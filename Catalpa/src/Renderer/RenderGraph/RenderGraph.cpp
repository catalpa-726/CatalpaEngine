#include "catpch.h"
#include "RenderGraph.h"
#include "RHI/RHIDevice.h"

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

        // 限定当前颜色附件路径能够处理的布局。
        bool IsSupportedLayout(RHITextureLayout layout) noexcept
        {
            return layout == RHITextureLayout::Undefined ||
                layout == RHITextureLayout::ColorAttachment ||
                layout == RHITextureLayout::Present;
        }

        // 保守覆盖同一队列此前对颜色附件的访问。
        RHITextureAccess ConservativeTextureBefore(RHITextureLayout layout) noexcept
        {
            return {
                .stage = RHIPipelineStage::AllCommands,
                .access = layout == RHITextureLayout::ColorAttachment ?
                    RHIResourceAccess::ColorReadWrite : RHIResourceAccess::None,
                .layout = layout
            };
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

        // 当前只导入本帧 Acquire 返回的一张交换链图像。
        if (!m_Textures.empty())
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        RHITextureStateSnapshot snapshot{};
        if (const RHIStatus status = m_Device.GetTextureState(view, snapshot); !status.IsSuccess()) return status;

        if (snapshot.extent.IsEmpty() || snapshot.state.revision == 0)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (snapshot.sampleCount != 1 || snapshot.format == PixelFormat::Unknown ||
            !IsSupportedLayout(snapshot.state.access.layout))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        m_Textures.push_back({ .handle = view, .snapshot = std::move(snapshot) });
        resource = { .graph = m_Id, .slot = {.index = 0, .generation = 1 } };
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

    RHIStatus RenderGraph::AddDrawPass(const char* name, const RGIndexedDrawDesc& desc)
    {
        constexpr const char* operation = "RenderGraph::AddDrawPass";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;

        const auto* vertices = Resolve(desc.vertices);
        const auto* indices = Resolve(desc.indices);
        const auto* color = Resolve(desc.color);

        if (!name || !*name || !vertices || !indices || !color ||
            !desc.pipeline.HasValue() || desc.pipeline.device != m_Device.GetDeviceId())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if ((desc.loadOp != RHILoadOp::Load && desc.loadOp != RHILoadOp::Clear && desc.loadOp != RHILoadOp::DontCare) ||
            (desc.storeOp != RHIStoreOp::Store && desc.storeOp != RHIStoreOp::DontCare))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        uint32_t indexSize = 0;

        switch (desc.indexType) {
        case RHIIndexType::UInt16: indexSize = 2; break;
        case RHIIndexType::UInt32: indexSize = 4; break;
        default: return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        const auto& vertexDesc = vertices->snapshot.desc;
        const auto& indexDesc = indices->snapshot.desc;

        if (!HasBufferUsage(vertexDesc.usage, RHIBufferUsage::Vertex) ||
            !HasBufferUsage(indexDesc.usage, RHIBufferUsage::Index) ||
            desc.vertexOffset >= vertexDesc.size || desc.vertexOffset % 4 != 0 ||
            desc.indexOffset >= indexDesc.size || desc.indexOffset % indexSize != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const uint64_t firstByte = static_cast<uint64_t>(desc.firstIndex) * indexSize;
        const uint64_t byteCount = static_cast<uint64_t>(desc.indexCount) * indexSize;

        if (!IsValidBufferRange(indexDesc.size - desc.indexOffset, firstByte, byteCount))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (m_Passes.size() == m_Passes.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        m_Passes.push_back({ .name = name, .isDraw = true, .draw = desc });
        return {};
    }

    RHIStatus RenderGraph::ExportPresent(RGTexture resource)
    {
        constexpr const char* operation = "RenderGraph::ExportPresent";
        if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess()) return status;
        if (!Resolve(resource)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

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

        for (const auto& pass : m_Passes) {
            RenderGraphPlan::Step step;
            step.name = pass.name;

            if (!pass.isDraw) {
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
            else {
                const auto& draw = pass.draw;
                const uint32_t vertices = draw.vertices.slot.index;
                const uint32_t indices = draw.indices.slot.index;
                const uint32_t color = draw.color.slot.index;

                const uint64_t vertexBytes = m_Buffers[vertices].snapshot.desc.size - draw.vertexOffset;
                const uint64_t indexSize = draw.indexType == RHIIndexType::UInt16 ? 2u : 4u;
                const uint64_t indexReadOffset = draw.indexOffset + static_cast<uint64_t>(draw.firstIndex) * indexSize;
                const uint64_t indexReadBytes = static_cast<uint64_t>(draw.indexCount) * indexSize;

                // 空 draw 仍执行附件操作，但不读取顶点/索引。
                if (draw.indexCount != 0 && draw.instanceCount != 0) {
                    if (!RangeContains(buffers[indices].initialized, indexReadOffset, indexReadBytes))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedIndexInput" };

                    // 当前不读取 GPU 索引值，保守要求绑定剩余顶点区间已初始化。
                    if (!RangeContains(buffers[vertices].initialized, draw.vertexOffset, vertexBytes))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedVertexInput" };
                }

                auto& colorState = textures[color];

                if (draw.loadOp == RHILoadOp::Load && !colorState.initialized)
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedAttachmentLoad" };

                step.kind = RenderGraphPlan::StepKind::Draw;
                step.draw = draw;
                step.vertices = m_Buffers[vertices].handle;
                step.indices = m_Buffers[indices].handle;
                step.color = m_Textures[color].handle;
                step.extent = m_Textures[color].snapshot.extent;

                if (vertices == indices) {
                    addBufferAccess(step, vertices, {
                        .stage = RHIPipelineStage::VertexInput,
                        .access = RHIResourceAccess::VertexIndexRead
                        });
                }
                else {
                    addBufferAccess(step, vertices, {
                        .stage = RHIPipelineStage::VertexInput,
                        .access = RHIResourceAccess::VertexRead
                        });

                    addBufferAccess(step, indices, {
                        .stage = RHIPipelineStage::VertexInput,
                        .access = RHIResourceAccess::IndexRead
                        });
                }

                const RHITextureAccess colorAccess{
                    .stage = RHIPipelineStage::ColorAttachmentOutput,
                    .access = draw.loadOp == RHILoadOp::Load ?
                        RHIResourceAccess::ColorReadWrite : RHIResourceAccess::ColorWrite,
                    .layout = RHITextureLayout::ColorAttachment
                };

                step.textureBarriers.push_back({
                    .view = step.color,
                    .before = ConservativeTextureBefore(colorState.access.layout),
                    .after = colorAccess
                    });

                colorState.access = colorAccess;

                // Record 使用完整 extent 作为 renderArea。
                if (draw.loadOp == RHILoadOp::Clear) colorState.initialized = true;
                else if (draw.loadOp == RHILoadOp::DontCare) colorState.initialized = false;

                if (draw.storeOp == RHIStoreOp::DontCare) colorState.initialized = false;
                textureUsed[color] = 1;
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