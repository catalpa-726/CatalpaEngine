#include "catpch.h"
#include "RenderGraph.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

    namespace {

        // 单线程分配图身份，耗尽后不回绕复用。
        uint64_t AllocateGraphId() noexcept
        {
            static uint64_t next = 1;
            const uint64_t result = next;
            if (next != 0) next = next == std::numeric_limits<uint64_t>::max() ? 0 : next + 1;
            return result;
        }

        // 限定当前颜色附件、上传和 fragment 采样能够处理的布局。
        bool IsSupportedLayout(RHITextureLayout layout) noexcept
        {
            return layout == RHITextureLayout::Undefined ||
                layout == RHITextureLayout::ColorAttachment ||
                layout == RHITextureLayout::DepthAttachment ||
                layout == RHITextureLayout::TransferDestination ||
                layout == RHITextureLayout::ShaderReadOnly ||
                layout == RHITextureLayout::Present;
        }

        // 保守覆盖同一队列此前在对应布局下的访问。
        RHITextureAccess ConservativeTextureBefore(RHITextureLayout layout) noexcept
        {
            RHIResourceAccess access = RHIResourceAccess::None;
            if (layout == RHITextureLayout::ColorAttachment) access = RHIResourceAccess::ColorReadWrite;
            else if (layout == RHITextureLayout::DepthAttachment) access = RHIResourceAccess::DepthReadWrite;
            else if (layout == RHITextureLayout::TransferDestination) access = RHIResourceAccess::TransferWrite;
            else if (layout == RHITextureLayout::ShaderReadOnly) access = RHIResourceAccess::SampledRead;

            return { .stage = RHIPipelineStage::AllCommands, .access = access, .layout = layout };
        }

    }

    RenderGraph::RenderGraph(RHIDevice& device) : m_Device(device), m_Id(AllocateGraphId()) {}

    RHIStatus RenderGraph::CheckBuilding(const char* operation) const
    {
        if (m_Id == 0) return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        if (m_Compiled || m_Present.HasValue()) return { .code = RHIErrorCode::InvalidState, .operation = operation };
        return {};
    }

    const RenderGraph::BufferResource* RenderGraph::Resolve(RGBuffer resource) const
    {
        if (!resource.HasValue() || resource.graph != m_Id ||
            resource.slot.generation != 1 || resource.slot.index >= m_Buffers.size())
            return nullptr;

        return &m_Buffers[resource.slot.index];
    }

    const RenderGraph::TextureResource* RenderGraph::Resolve(RGTexture resource) const
    {
        if (!resource.HasValue() || resource.graph != m_Id ||
            resource.slot.generation != 1 || resource.slot.index >= m_Textures.size())
            return nullptr;

        return &m_Textures[resource.slot.index];
    }

    RHIStatus RenderGraph::ImportBuffer(BufferHandle buffer, RGBuffer& resource)
    {
        constexpr const char* operation = "RenderGraph::ImportBuffer";
        resource = {};

        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        if (!buffer.HasValue() || buffer.device != m_Device.GetDeviceId())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        for (size_t i = 0; i < m_Buffers.size(); ++i) {
            if (m_Buffers[i].handle == buffer) {
                resource = { .graph = m_Id, .slot = {.index = static_cast<uint32_t>(i), .generation = 1 } };
                return {};
            }
        }

        if (m_Buffers.size() >= GenerationalHandle<RGBufferTag>::InvalidIndex ||
            m_Buffers.size() == m_Buffers.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        RHIBufferStateSnapshot snapshot{};
        if (const RHIStatus status = m_Device.GetBufferState(buffer, snapshot); !status.IsSuccess()) return status;

        if (snapshot.desc.size == 0 || snapshot.state.revision == 0)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        const uint32_t index = static_cast<uint32_t>(m_Buffers.size());
        m_Buffers.push_back({ .handle = buffer, .snapshot = std::move(snapshot) });
        resource = { .graph = m_Id, .slot = {.index = index, .generation = 1 } };
        return {};
    }

    RHIStatus RenderGraph::ImportTexture(TextureViewHandle view, RGTexture& resource)
    {
        constexpr const char* operation = "RenderGraph::ImportTexture";
        resource = {};

        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        if (!view.HasValue() || view.device != m_Device.GetDeviceId())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        for (size_t i = 0; i < m_Textures.size(); ++i) {
            if (m_Textures[i].handle == view) {
                resource = { .graph = m_Id, .slot = {.index = static_cast<uint32_t>(i), .generation = 1 } };
                return {};
            }
        }

        // 允许同时导入普通纹理和交换链图像，限制图内句柄索引及容器容量。
        if (m_Textures.size() >= GenerationalHandle<RGTextureTag>::InvalidIndex ||
            m_Textures.size() == m_Textures.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        RHITextureStateSnapshot snapshot{};
        if (const RHIStatus status = m_Device.GetTextureState(view, snapshot); !status.IsSuccess()) return status;

        if (snapshot.extent.IsEmpty() || snapshot.state.revision == 0)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (snapshot.sampleCount != 1 || snapshot.format == PixelFormat::Unknown ||
            !IsSupportedLayout(snapshot.state.access.layout))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const uint32_t index = static_cast<uint32_t>(m_Textures.size());
        m_Textures.push_back({ .handle = view, .snapshot = std::move(snapshot) });
        resource = { .graph = m_Id, .slot = {.index = index, .generation = 1 } };
        return {};
    }

    RHIStatus RenderGraph::AddCopyPass(const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy)
    {
        constexpr const char* operation = "RenderGraph::AddCopyPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        const auto* sourceResource = Resolve(source);
        const auto* destinationResource = Resolve(destination);

        if (!name || !*name || !sourceResource || !destinationResource || copy.size == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto& sourceDesc = sourceResource->snapshot.desc;
        const auto& destinationDesc = destinationResource->snapshot.desc;

        if (!HasBufferUsage(sourceDesc.usage, RHIBufferUsage::TransferSource) ||
            !HasBufferUsage(destinationDesc.usage, RHIBufferUsage::TransferDestination) ||
            !IsValidBufferRange(sourceDesc.size, copy.sourceOffset, copy.size) ||
            !IsValidBufferRange(destinationDesc.size, copy.destinationOffset, copy.size))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (source == destination &&
            copy.sourceOffset < copy.destinationOffset + copy.size &&
            copy.destinationOffset < copy.sourceOffset + copy.size)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        m_Passes.push_back({
            .name = name,
            .source = source,
            .destination = destination,
            .copy = copy
            });

        return {};
    }

    RHIStatus RenderGraph::AddTextureUploadPass(
        const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy)
    {
        constexpr const char* operation = "RenderGraph::AddTextureUploadPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        const auto* sourceResource = Resolve(source);
        const auto* destinationResource = Resolve(destination);

        if (!name || !*name || !sourceResource || !destinationResource)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto& sourceDesc = sourceResource->snapshot.desc;
        const auto& destinationSnapshot = destinationResource->snapshot;

        if (!HasBufferUsage(sourceDesc.usage, RHIBufferUsage::TransferSource) ||
            !HasTextureUsage(destinationSnapshot.usage, RHITextureUsage::TransferDestination) ||
            destinationSnapshot.presentable || copy.bufferOffset % 4 != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (copy.range != RHITextureSubresourceRange{})
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        uint64_t bytes = 0;

        if (!TryGetTextureUploadByteSize(destinationSnapshot.extent, destinationSnapshot.format, bytes))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        if (!IsValidBufferRange(sourceDesc.size, copy.bufferOffset, bytes))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        m_Passes.push_back({
            .name = name,
            .kind = PassKind::TextureUpload,
            .source = source,
            .destinationTexture = destination,
            .textureCopy = copy,
            .textureBytes = bytes
            });

        return {};
    }

    RHIStatus RenderGraph::AddDrawPass(const char* name, const RGIndexedDrawDesc& desc)
    {
        constexpr const char* operation = "RenderGraph::AddDrawPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;
        if (desc.pushConstants.size() % 4 != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (desc.pushConstants.size() > m_Device.GetCapabilities().maxPushConstantBytes)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const auto* vertices = Resolve(desc.vertices);
        const auto* indices = Resolve(desc.indices);
        const auto* color = Resolve(desc.color);
        const auto* depth = Resolve(desc.depth);
        if (!name || !*name || !vertices || !indices || (!color && !depth)
            || (!color && desc.color != RGTexture{}) || (!depth && desc.depth != RGTexture{})
            || !desc.pipeline.HasValue() || desc.pipeline.device != m_Device.GetDeviceId())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto validLoad = [](RHILoadOp value) { return value == RHILoadOp::Load || value == RHILoadOp::Clear || value == RHILoadOp::DontCare; };
        const auto validStore = [](RHIStoreOp value) { return value == RHIStoreOp::Store || value == RHIStoreOp::DontCare; };
        if (color) {
            if (GetPixelFormatInfo(color->snapshot.format).isDepth
                || !HasTextureUsage(color->snapshot.usage, RHITextureUsage::ColorAttachment)
                || !validLoad(desc.loadOp) || !validStore(desc.storeOp))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            if (desc.loadOp == RHILoadOp::Clear)
                for (const float value : desc.clearColor)
                    if (!std::isfinite(value)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }
        if (depth) {
            if (!GetPixelFormatInfo(depth->snapshot.format).isDepth || depth->snapshot.presentable
                || !HasTextureUsage(depth->snapshot.usage, RHITextureUsage::DepthAttachment)
                || !validLoad(desc.depthLoadOp) || !validStore(desc.depthStoreOp)
                || !std::isfinite(desc.clearDepth) || desc.clearDepth < 0.0f || desc.clearDepth > 1.0f
                || (desc.depthReadOnly && (desc.depthLoadOp != RHILoadOp::Load || desc.depthStoreOp != RHIStoreOp::Store))
                || (color && (desc.depth == desc.color || color->snapshot.extent != depth->snapshot.extent)))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }
        const auto extent = color ? color->snapshot.extent : depth->snapshot.extent;
        if (extent.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            || extent.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            || !std::isfinite(desc.viewport.x) || !std::isfinite(desc.viewport.y)
            || !std::isfinite(desc.viewport.width) || !std::isfinite(desc.viewport.height)
            || !std::isfinite(desc.viewport.minDepth) || !std::isfinite(desc.viewport.maxDepth)
            || desc.viewport.width <= 0.0f || desc.viewport.height <= 0.0f
            || desc.viewport.minDepth < 0.0f || desc.viewport.maxDepth > 1.0f
            || desc.viewport.minDepth > desc.viewport.maxDepth)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (!desc.scissor.IsEmpty()) {
            if (desc.scissor.offset.x < 0 || desc.scissor.offset.y < 0
                || static_cast<uint32_t>(desc.scissor.offset.x) > extent.width
                || desc.scissor.extent.width > extent.width - static_cast<uint32_t>(desc.scissor.offset.x)
                || static_cast<uint32_t>(desc.scissor.offset.y) > extent.height
                || desc.scissor.extent.height > extent.height - static_cast<uint32_t>(desc.scissor.offset.y))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        if (desc.sampledTextures.size() > RHIMaxBindingsPerSet / 2
            || desc.uniformBuffers.size() > RHIMaxBindingsPerSet - desc.sampledTextures.size() * 2)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        for (size_t i = 0; i < desc.sampledTextures.size(); ++i) {
            const auto& binding = desc.sampledTextures[i];
            const auto* sampled = Resolve(binding.texture);
            if (!sampled || binding.texture == desc.color || binding.texture == desc.depth
                || GetPixelFormatInfo(sampled->snapshot.format).isDepth
                || !HasTextureUsage(sampled->snapshot.usage, RHITextureUsage::Sampled)
                || !binding.sampler.HasValue() || binding.sampler.device != m_Device.GetDeviceId()
                || binding.textureBinding == binding.samplerBinding)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (size_t j = 0; j < i; ++j) {
                const auto& previous = desc.sampledTextures[j];
                if (binding.textureBinding == previous.textureBinding || binding.textureBinding == previous.samplerBinding
                    || binding.samplerBinding == previous.textureBinding || binding.samplerBinding == previous.samplerBinding)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
        }
        const auto& caps = m_Device.GetCapabilities();
        for (size_t i = 0; i < desc.uniformBuffers.size(); ++i) {
            const auto& binding = desc.uniformBuffers[i];
            const auto* uniform = Resolve(binding.buffer);
            if (!uniform || binding.buffer == desc.vertices || binding.buffer == desc.indices
                || !HasBufferUsage(uniform->snapshot.desc.usage, RHIBufferUsage::Uniform) || binding.size == 0
                || !IsValidBufferRange(uniform->snapshot.desc.size, binding.offset, binding.size)
                || (binding.visibility != RHIShaderVisibility::Vertex && binding.visibility != RHIShaderVisibility::Fragment
                    && binding.visibility != RHIShaderVisibility::VertexFragment))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            if (caps.minUniformBufferOffsetAlignment == 0 || binding.offset % caps.minUniformBufferOffsetAlignment != 0
                || binding.size > caps.maxUniformBufferRange)
                return { .code = RHIErrorCode::Unsupported, .operation = operation };
            for (size_t j = 0; j < i; ++j)
                if (binding.binding == desc.uniformBuffers[j].binding)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (const auto& sampled : desc.sampledTextures)
                if (binding.binding == sampled.textureBinding || binding.binding == sampled.samplerBinding)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        uint32_t indexSize = 0;
        switch (desc.indexType) {
        case RHIIndexType::UInt16: indexSize = 2; break;
        case RHIIndexType::UInt32: indexSize = 4; break;
        default: return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }
        const auto& vertexDesc = vertices->snapshot.desc;
        const auto& indexDesc = indices->snapshot.desc;
        if (!HasBufferUsage(vertexDesc.usage, RHIBufferUsage::Vertex)
            || !HasBufferUsage(indexDesc.usage, RHIBufferUsage::Index)
            || desc.vertexOffset >= vertexDesc.size || desc.vertexOffset % 4 != 0
            || desc.indexOffset >= indexDesc.size || desc.indexOffset % indexSize != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        const uint64_t firstByte = static_cast<uint64_t>(desc.firstIndex) * indexSize;
        const uint64_t byteCount = static_cast<uint64_t>(desc.indexCount) * indexSize;
        if (!IsValidBufferRange(indexDesc.size - desc.indexOffset, firstByte, byteCount))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        m_Passes.push_back({ .name = name, .kind = PassKind::Draw, .draw = desc });
        return {};
    }

    RHIStatus RenderGraph::AddClearPass(const char* name, RGTexture color, std::array<float, 4> clearColor)
    {
        constexpr const char* operation = "RenderGraph::AddClearPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;
        const auto* target = Resolve(color);
        if (!name || !*name || !target || GetPixelFormatInfo(target->snapshot.format).isDepth
            || !HasTextureUsage(target->snapshot.usage, RHITextureUsage::ColorAttachment))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        for (const float value : clearColor)
            if (!std::isfinite(value)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        m_Passes.push_back({ .name = name, .kind = PassKind::Clear, .clearTexture = color, .clearColor = clearColor });
        return {};
    }

    RHIStatus RenderGraph::ExportPresent(RGTexture resource)
    {
        constexpr const char* operation = "RenderGraph::ExportPresent";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        const auto* texture = Resolve(resource);

        if (!texture || !texture->snapshot.presentable)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        m_Present = resource;
        return {};
    }

    RHIStatus RenderGraph::Compile(RenderGraphPlan& plan)
    {
        constexpr const char* operation = "RenderGraph::Compile";
        plan = RenderGraphPlan{};

        if (m_Id == 0) return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        if (m_Compiled || !Resolve(m_Present)) return { .code = RHIErrorCode::InvalidState, .operation = operation };

        RenderGraphPlan compiled;

        if (m_Passes.size() >= compiled.m_Steps.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        compiled.m_Steps.reserve(m_Passes.size() + 1);
        compiled.m_BufferUpdates.reserve(m_Buffers.size());
        compiled.m_TextureUpdates.reserve(m_Textures.size());

        std::vector<RHIBufferState> buffers;
        std::vector<RHITextureState> textures;
        std::vector<uint8_t> bufferUsed(m_Buffers.size(), 0);
        std::vector<uint8_t> textureUsed(m_Textures.size(), 0);

        buffers.reserve(m_Buffers.size());
        textures.reserve(m_Textures.size());

        for (const auto& resource : m_Buffers) buffers.push_back(resource.snapshot.state);
        for (const auto& resource : m_Textures) textures.push_back(resource.snapshot.state);

        // 添加整段 Buffer 屏障，并更新当前编译中的状态副本。
        const auto addBufferAccess = [&](RenderGraphPlan::Step& step, uint32_t index, RHIBufferAccess after) {
            step.bufferBarriers.push_back({
                .buffer = m_Buffers[index].handle,
                .offset = 0,
                .size = m_Buffers[index].snapshot.desc.size,
                .before = {.stage = RHIPipelineStage::AllCommands, .access = RHIResourceAccess::MemoryReadWrite },
                .after = after
                });

            buffers[index].access = after;
            bufferUsed[index] = 1;
            };

        // 附件访问/初始化与实际BeginRendering使用同一load/store描述，不提前发布设备状态。
        const auto addAttachmentAccess = [&](RenderGraphPlan::Step& step, uint32_t index,
            RHILoadOp loadOp, RHIStoreOp storeOp, RHITextureAccess after) -> RHIStatus {
            auto& state = textures[index];
            if (loadOp == RHILoadOp::Load && !state.initialized)
                return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedAttachmentLoad" };
            RHITextureSubresourceRange range{};
            range.aspect = GetPixelFormatInfo(m_Textures[index].snapshot.format).isDepth ? RHITextureAspect::Depth : RHITextureAspect::Color;
            step.textureBarriers.push_back({
                .view = m_Textures[index].handle, .range = range,
                .before = ConservativeTextureBefore(state.access.layout), .after = after
            });
            state.access = after;
            if (loadOp == RHILoadOp::Clear) state.initialized = true;
            else if (loadOp == RHILoadOp::DontCare) state.initialized = false;
            if (storeOp == RHIStoreOp::DontCare) state.initialized = false;
            textureUsed[index] = 1;
            return {};
        };

        for (const auto& pass : m_Passes) {
            RenderGraphPlan::Step step;
            step.name = pass.name;

            if (pass.kind == PassKind::Copy) {
                const uint32_t source = pass.source.slot.index;
                const uint32_t destination = pass.destination.slot.index;

                if (!RangeContains(buffers[source].initialized, pass.copy.sourceOffset, pass.copy.size))
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedCopySource" };

                step.kind = RenderGraphPlan::StepKind::Copy;
                step.source = m_Buffers[source].handle;
                step.destination = m_Buffers[destination].handle;
                step.copy = pass.copy;

                if (source == destination) {
                    addBufferAccess(step, source, {
                        .stage = RHIPipelineStage::Transfer,
                        .access = RHIResourceAccess::TransferReadWrite
                        });
                }
                else {
                    addBufferAccess(step, source, {
                        .stage = RHIPipelineStage::Transfer,
                        .access = RHIResourceAccess::TransferRead
                        });

                    addBufferAccess(step, destination, {
                        .stage = RHIPipelineStage::Transfer,
                        .access = RHIResourceAccess::TransferWrite
                        });
                }

                MarkInitialized(buffers[destination].initialized, pass.copy.destinationOffset, pass.copy.size);
            }
            else if (pass.kind == PassKind::TextureUpload) {
                const uint32_t source = pass.source.slot.index;
                const uint32_t destination = pass.destinationTexture.slot.index;

                if (!RangeContains(buffers[source].initialized, pass.textureCopy.bufferOffset, pass.textureBytes))
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedTextureUploadSource" };

                step.kind = RenderGraphPlan::StepKind::TextureUpload;
                step.source = m_Buffers[source].handle;
                step.destinationView = m_Textures[destination].handle;
                step.textureCopy = pass.textureCopy;

                addBufferAccess(step, source, {
                    .stage = RHIPipelineStage::Transfer,
                    .access = RHIResourceAccess::TransferRead
                    });

                auto& destinationState = textures[destination];

                const RHITextureAccess destinationAccess{
                    .stage = RHIPipelineStage::Transfer,
                    .access = RHIResourceAccess::TransferWrite,
                    .layout = RHITextureLayout::TransferDestination
                };

                step.textureBarriers.push_back({
                    .view = step.destinationView,
                    .range = pass.textureCopy.range,
                    .before = ConservativeTextureBefore(destinationState.access.layout),
                    .after = destinationAccess
                    });

                destinationState.access = destinationAccess;
                destinationState.initialized = true;
                textureUsed[destination] = 1;
            }
            else if (pass.kind == PassKind::Clear) {
                const uint32_t color = pass.clearTexture.slot.index;
                step.kind = RenderGraphPlan::StepKind::Clear;
                step.color = m_Textures[color].handle;
                step.extent = m_Textures[color].snapshot.extent;
                step.draw.clearColor = pass.clearColor;
                if (const auto status = addAttachmentAccess(step, color, RHILoadOp::Clear, RHIStoreOp::Store, {
                    .stage = RHIPipelineStage::ColorAttachmentOutput,
                    .access = RHIResourceAccess::ColorWrite, .layout = RHITextureLayout::ColorAttachment
                }); !status.IsSuccess()) return status;
            }
            else {
                const auto& draw = pass.draw;
                const uint32_t vertices = draw.vertices.slot.index;
                const uint32_t indices = draw.indices.slot.index;
                const uint64_t vertexBytes = m_Buffers[vertices].snapshot.desc.size - draw.vertexOffset;
                const uint64_t indexSize = draw.indexType == RHIIndexType::UInt16 ? 2u : 4u;
                const uint64_t indexReadOffset = draw.indexOffset + static_cast<uint64_t>(draw.firstIndex) * indexSize;
                const uint64_t indexReadBytes = static_cast<uint64_t>(draw.indexCount) * indexSize;
                if (draw.indexCount != 0 && draw.instanceCount != 0) {
                    if (!RangeContains(buffers[indices].initialized, indexReadOffset, indexReadBytes))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedIndexInput" };
                    if (!RangeContains(buffers[vertices].initialized, draw.vertexOffset, vertexBytes))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedVertexInput" };
                }
                step.kind = RenderGraphPlan::StepKind::Draw;
                step.draw = draw;
                step.vertices = m_Buffers[vertices].handle;
                step.indices = m_Buffers[indices].handle;
                step.extent = draw.color.HasValue() ? m_Textures[draw.color.slot.index].snapshot.extent
                    : m_Textures[draw.depth.slot.index].snapshot.extent;
                step.bindings.reserve(draw.sampledTextures.size() * 2 + draw.uniformBuffers.size());
                for (const auto& binding : draw.sampledTextures) {
                    const uint32_t sampled = binding.texture.slot.index;
                    const TextureViewHandle view = m_Textures[sampled].handle;
                    auto& sampledState = textures[sampled];
                    if (!sampledState.initialized)
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedSampledTexture" };
                    const RHITextureAccess sampledAccess{
                        .stage = RHIPipelineStage::FragmentShader,
                        .access = RHIResourceAccess::SampledRead,
                        .layout = RHITextureLayout::ShaderReadOnly
                    };
                    if (sampledState.access.stage != sampledAccess.stage || sampledState.access.access != sampledAccess.access
                        || sampledState.access.layout != sampledAccess.layout) {
                        step.textureBarriers.push_back({
                            .view = view, .before = ConservativeTextureBefore(sampledState.access.layout), .after = sampledAccess
                        });
                    }
                    sampledState.access = sampledAccess;
                    textureUsed[sampled] = 1;
                    step.bindings.push_back({ .binding = binding.textureBinding, .type = RHIBindingType::SampledTexture, .texture = view });
                    step.bindings.push_back({ .binding = binding.samplerBinding, .type = RHIBindingType::Sampler, .sampler = binding.sampler });
                }
                for (size_t i = 0; i < draw.uniformBuffers.size(); ++i) {
                    const auto& binding = draw.uniformBuffers[i];
                    const uint32_t uniform = binding.buffer.slot.index;
                    if (!RangeContains(buffers[uniform].initialized, binding.offset, binding.size))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedUniformInput" };
                    step.bindings.push_back({
                        .binding = binding.binding, .type = RHIBindingType::UniformBuffer,
                        .buffer = m_Buffers[uniform].handle, .offset = binding.offset, .size = binding.size
                    });
                    bool firstUse = true;
                    for (size_t j = 0; j < i; ++j)
                        if (draw.uniformBuffers[j].buffer == binding.buffer) firstUse = false;
                    if (!firstUse) continue;
                    // 同一buffer的多个范围合并stage，避免后一个绑定把本draw的前一可见阶段覆盖。
                    bool vertex = false, fragment = false;
                    for (const auto& use : draw.uniformBuffers) {
                        if (use.buffer != binding.buffer) continue;
                        vertex = vertex || use.visibility == RHIShaderVisibility::Vertex || use.visibility == RHIShaderVisibility::VertexFragment;
                        fragment = fragment || use.visibility == RHIShaderVisibility::Fragment || use.visibility == RHIShaderVisibility::VertexFragment;
                    }
                    addBufferAccess(step, uniform, {
                        .stage = vertex && fragment ? RHIPipelineStage::VertexFragmentShader
                            : vertex ? RHIPipelineStage::VertexShader : RHIPipelineStage::FragmentShader,
                        .access = RHIResourceAccess::UniformRead
                    });
                }
                if (vertices == indices) {
                    addBufferAccess(step, vertices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::VertexIndexRead });
                } else {
                    addBufferAccess(step, vertices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::VertexRead });
                    addBufferAccess(step, indices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::IndexRead });
                }
                if (draw.color.HasValue()) {
                    const uint32_t color = draw.color.slot.index;
                    step.color = m_Textures[color].handle;
                    if (const auto status = addAttachmentAccess(step, color, draw.loadOp, draw.storeOp, {
                        .stage = RHIPipelineStage::ColorAttachmentOutput,
                        .access = draw.loadOp == RHILoadOp::Load ? RHIResourceAccess::ColorReadWrite : RHIResourceAccess::ColorWrite,
                        .layout = RHITextureLayout::ColorAttachment
                    }); !status.IsSuccess()) return status;
                }
                if (draw.depth.HasValue()) {
                    const uint32_t depth = draw.depth.slot.index;
                    step.depth = m_Textures[depth].handle;
                    if (const auto status = addAttachmentAccess(step, depth, draw.depthLoadOp, draw.depthStoreOp, {
                        .stage = RHIPipelineStage::DepthStencilTests,
                        .access = draw.depthReadOnly ? RHIResourceAccess::DepthRead : RHIResourceAccess::DepthReadWrite,
                        .layout = RHITextureLayout::DepthAttachment
                    }); !status.IsSuccess()) return status;
                }
            }

            compiled.m_Steps.push_back(std::move(step));
        }

        const uint32_t present = m_Present.slot.index;
        auto& presentState = textures[present];

        if (!presentState.initialized)
            return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedPresent" };

        RenderGraphPlan::Step presentStep;
        presentStep.name = "Present";
        presentStep.kind = RenderGraphPlan::StepKind::Transition;
        presentStep.color = m_Textures[present].handle;
        presentStep.extent = m_Textures[present].snapshot.extent;

        const RHITextureAccess presentAccess{ .layout = RHITextureLayout::Present };

        presentStep.textureBarriers.push_back({
            .view = presentStep.color,
            .before = ConservativeTextureBefore(presentState.access.layout),
            .after = presentAccess
            });

        presentState.access = presentAccess;

        // 当前交换链 clipped = VK_TRUE，不承诺下一次 Acquire 后可以 Load 旧内容。
        presentState.initialized = false;
        textureUsed[present] = 1;
        compiled.m_Steps.push_back(std::move(presentStep));

        for (size_t i = 0; i < m_Buffers.size(); ++i) {
            if (!bufferUsed[i]) continue;

            const uint64_t revision = m_Buffers[i].snapshot.state.revision;

            if (revision == std::numeric_limits<uint64_t>::max())
                return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraph::BufferRevision" };

            buffers[i].revision = revision + 1;

            compiled.m_BufferUpdates.push_back({
                .buffer = m_Buffers[i].handle,
                .expectedRevision = revision,
                .after = std::move(buffers[i])
                });
        }

        for (size_t i = 0; i < m_Textures.size(); ++i) {
            if (!textureUsed[i]) continue;

            const uint64_t revision = m_Textures[i].snapshot.state.revision;

            if (revision == std::numeric_limits<uint64_t>::max())
                return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraph::TextureRevision" };

            textures[i].revision = revision + 1;

            compiled.m_TextureUpdates.push_back({
                .view = m_Textures[i].handle,
                .expectedRevision = revision,
                .after = std::move(textures[i])
                });
        }

        compiled.m_Valid = true;
        plan = std::move(compiled);
        m_Compiled = true;
        return {};
    }

}