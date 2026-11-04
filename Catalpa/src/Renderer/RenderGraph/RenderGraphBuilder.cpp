#include "catpch.h"
#include "RenderGraphBuilder.h"
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


    }

    RenderGraphBuilder::RenderGraphBuilder(RHIDevice& device) : m_Device(device), m_Id(AllocateGraphId()) {}

    RHIStatus RenderGraphBuilder::CheckBuilding(const char* operation) const
    {
        if (m_Id == 0) return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        if (m_Compiled || m_Present.HasValue()) return { .code = RHIErrorCode::InvalidState, .operation = operation };
        return {};
    }

    const RenderGraphBuilder::BufferResource* RenderGraphBuilder::Resolve(RGBuffer resource) const
    {
        if (!resource.HasValue() || resource.graph != m_Id ||
            resource.slot.generation != 1 || resource.slot.index >= m_Buffers.size())
            return nullptr;

        return &m_Buffers[resource.slot.index];
    }

    const RenderGraphBuilder::TextureResource* RenderGraphBuilder::Resolve(RGTexture resource) const
    {
        if (!resource.HasValue() || resource.graph != m_Id ||
            resource.slot.generation != 1 || resource.slot.index >= m_Textures.size())
            return nullptr;

        return &m_Textures[resource.slot.index];
    }

    RHIStatus RenderGraphBuilder::ImportBuffer(BufferHandle buffer, RGBuffer& resource)
    {
        constexpr const char* operation = "RenderGraphBuilder::ImportBuffer";
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

    RHIStatus RenderGraphBuilder::ImportTexture(TextureViewHandle view, RGTexture& resource)
    {
        constexpr const char* operation = "RenderGraphBuilder::ImportTexture";
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

    RHIStatus RenderGraphBuilder::AddCopyPass(const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy)
    {
        constexpr const char* operation = "RenderGraphBuilder::AddCopyPass";
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

        if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        m_Passes.push_back({
            .name = name,
            .source = source,
            .destination = destination,
            .copy = copy
            });

        return {};
    }

    RHIStatus RenderGraphBuilder::AddTextureUploadPass(
        const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy)
    {
        constexpr const char* operation = "RenderGraphBuilder::AddTextureUploadPass";
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

        if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
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

    RHIStatus RenderGraphBuilder::AddDrawPass(const char* name, const RGIndexedDrawDesc& desc)
    {
        constexpr const char* operation = "RenderGraphBuilder::AddDrawPass";
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
        if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        m_Passes.push_back({ .name = name, .kind = PassKind::Draw, .draw = desc });
        return {};
    }

    RHIStatus RenderGraphBuilder::AddClearPass(const char* name, RGTexture color, std::array<float, 4> clearColor)
    {
        constexpr const char* operation = "RenderGraphBuilder::AddClearPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;
        const auto* target = Resolve(color);
        if (!name || !*name || !target || GetPixelFormatInfo(target->snapshot.format).isDepth
            || !HasTextureUsage(target->snapshot.usage, RHITextureUsage::ColorAttachment))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        for (const float value : clearColor)
            if (!std::isfinite(value)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        m_Passes.push_back({ .name = name, .kind = PassKind::Clear, .clearTexture = color, .clearColor = clearColor });
        return {};
    }

    RHIStatus RenderGraphBuilder::ExportPresent(RGTexture resource)
    {
        constexpr const char* operation = "RenderGraphBuilder::ExportPresent";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        const auto* texture = Resolve(resource);

        if (!texture || !texture->snapshot.presentable)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        m_Present = resource;
        return {};
    }

    RGPass RenderGraphBuilder::GetLastPass() const noexcept
    {
        if (m_Passes.empty()) return {};
        return { .graph = m_Id, .slot = { .index = static_cast<uint32_t>(m_Passes.size() - 1), .generation = 1 } };
    }

    RHIStatus RenderGraphBuilder::AddDependency(RGPass before, RGPass after) try
    {
        constexpr const char* operation = "RenderGraphBuilder::AddDependency";
        if (const auto status = CheckBuilding(operation); !status.IsSuccess()) return status;
        const auto valid = [&](RGPass pass) {
            return pass.HasValue() && pass.graph == m_Id && pass.slot.generation == 1 && pass.slot.index < m_Passes.size();
        };
        if (!valid(before) || !valid(after) || before == after)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        const RGDependency edge{ .before = before.slot.index, .after = after.slot.index };
        if (std::find(m_Dependencies.begin(), m_Dependencies.end(), edge) == m_Dependencies.end())
            m_Dependencies.push_back(edge);
        return {};
    }
    catch (const std::bad_alloc&) {
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphBuilder::DependencyAllocation" };
    }
    catch (const std::length_error&) {
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphBuilder::DependencyCapacity" };
    }

    void RenderGraphBuilder::Describe(
        std::vector<RGResourceDeclaration>& resources, std::vector<RGPassDeclaration>& passes) const
    {
        resources.clear();
        passes.clear();
        resources.reserve(m_Buffers.size() + m_Textures.size());
        passes.reserve(m_Passes.size());
        for (const auto& buffer : m_Buffers)
            resources.push_back({ .kind = RGResourceKind::Buffer, .size = buffer.snapshot.desc.size,
                .initialized = buffer.snapshot.state.initialized });
        for (const auto& texture : m_Textures) {
            RGResourceDeclaration resource{ .kind = RGResourceKind::Texture, .size = 1 };
            if (texture.snapshot.state.initialized) resource.initialized.push_back({ .offset = 0, .size = 1 });
            resources.push_back(std::move(resource));
        }
        const auto textureIndex = [&](RGTexture texture) {
            return static_cast<uint32_t>(m_Buffers.size()) + texture.slot.index;
        };
        for (const auto& pass : m_Passes) {
            RGPassDeclaration declaration;
            // UploadManager 在整批提交后统一认领已上传资源，两类复制均为不可剔除副作用。
            declaration.sideEffect = pass.kind == PassKind::Copy || pass.kind == PassKind::TextureUpload;
            const auto readBuffer = [&](RGBuffer buffer, uint64_t offset, uint64_t size) {
                if (size != 0) declaration.accesses.push_back({ .resource = buffer.slot.index, .offset = offset, .size = size });
            };
            const auto attachment = [&](RGTexture texture, RHILoadOp load, RHIStoreOp store, bool readOnly) {
                if (!texture.HasValue()) return;
                declaration.accesses.push_back({ .resource = textureIndex(texture), .size = 1,
                    .mode = readOnly ? RGAccessMode::Read : load == RHILoadOp::Load ? RGAccessMode::ReadWrite : RGAccessMode::Write,
                    .initialization = readOnly ? RGWriteInitialization::Preserve
                        : store == RHIStoreOp::DontCare || load == RHILoadOp::DontCare ? RGWriteInitialization::Discard
                        : load == RHILoadOp::Clear ? RGWriteInitialization::Initialize : RGWriteInitialization::Preserve });
            };
            switch (pass.kind) {
            case PassKind::Copy:
                readBuffer(pass.source, pass.copy.sourceOffset, pass.copy.size);
                declaration.accesses.push_back({ .resource = pass.destination.slot.index,
                    .offset = pass.copy.destinationOffset, .size = pass.copy.size,
                    .mode = RGAccessMode::Write, .initialization = RGWriteInitialization::Initialize });
                break;
            case PassKind::TextureUpload:
                readBuffer(pass.source, pass.textureCopy.bufferOffset, pass.textureBytes);
                declaration.accesses.push_back({ .resource = textureIndex(pass.destinationTexture), .size = 1,
                    .mode = RGAccessMode::Write, .initialization = RGWriteInitialization::Initialize });
                break;
            case PassKind::Clear:
                attachment(pass.clearTexture, RHILoadOp::Clear, RHIStoreOp::Store, false);
                break;
            case PassKind::Draw: {
                const auto& draw = pass.draw;
                if (draw.indexCount != 0 && draw.instanceCount != 0) {
                    readBuffer(draw.vertices, draw.vertexOffset, m_Buffers[draw.vertices.slot.index].snapshot.desc.size - draw.vertexOffset);
                    const uint64_t indexSize = draw.indexType == RHIIndexType::UInt16 ? 2u : 4u;
                    readBuffer(draw.indices, draw.indexOffset + static_cast<uint64_t>(draw.firstIndex) * indexSize,
                        static_cast<uint64_t>(draw.indexCount) * indexSize);
                }
                for (const auto& uniform : draw.uniformBuffers) readBuffer(uniform.buffer, uniform.offset, uniform.size);
                for (const auto& sampled : draw.sampledTextures)
                    declaration.accesses.push_back({ .resource = textureIndex(sampled.texture), .size = 1 });
                attachment(draw.color, draw.loadOp, draw.storeOp, false);
                attachment(draw.depth, draw.depthLoadOp, draw.depthStoreOp, draw.depthReadOnly);
                break;
            }
            }
            passes.push_back(std::move(declaration));
        }
    }

}
