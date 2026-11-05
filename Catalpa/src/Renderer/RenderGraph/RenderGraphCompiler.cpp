#include "catpch.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphBuilder.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {
    namespace {
        // 保守覆盖同一队列此前在对应布局下的访问。
        RHITextureAccess ConservativeTextureBefore(RHITextureLayout layout) noexcept
        {
            RHIResourceAccess access = RHIResourceAccess::None;
            if (layout == RHITextureLayout::ColorAttachment) access = RHIResourceAccess::ColorReadWrite;
            else if (layout == RHITextureLayout::DepthAttachment) access = RHIResourceAccess::DepthReadWrite;
            else if (layout == RHITextureLayout::TransferDestination) access = RHIResourceAccess::TransferWrite;
            else if (layout == RHITextureLayout::TransferSource) access = RHIResourceAccess::TransferRead;
            else if (layout == RHITextureLayout::ShaderReadOnly) access = RHIResourceAccess::SampledRead;

            return { .stage = RHIPipelineStage::AllCommands, .access = access, .layout = layout };
        }

    }

    RHIStatus RenderGraphCompiler::BuildSchedule(
        std::span<const RGResourceDeclaration> resources, std::span<const RGPassDeclaration> passes,
        std::span<const RGDependency> dependencies, std::span<const RGResourceAccess> exports,
        RGCompiledSchedule& schedule) try
    {
        schedule = {};
        constexpr const char* operation = "RenderGraphCompiler::BuildSchedule";
        constexpr uint32_t noPass = std::numeric_limits<uint32_t>::max();
        if (resources.size() >= noPass || passes.size() >= noPass)
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        // 区间前沿保留最近写者和其后的全部读者；局部覆盖不会丢失未覆盖范围的版本。
        struct Segment {
            uint64_t begin = 0, end = 0, version = 0;
            uint32_t writer = std::numeric_limits<uint32_t>::max();
            std::vector<uint32_t> readers;
            bool initialized = false;
        };
        std::vector<std::vector<Segment>> states(resources.size());
        std::vector<uint64_t> nextVersion(resources.size(), 0);
        const auto split = [](std::vector<Segment>& segments, uint64_t at) {
            for (size_t i = 0; i < segments.size(); ++i) {
                if (at <= segments[i].begin || at >= segments[i].end) continue;
                Segment right = segments[i];
                right.begin = at;
                segments[i].end = at;
                segments.insert(segments.begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(right));
                break;
            }
        };
        for (size_t i = 0; i < resources.size(); ++i) {
            const auto& resource = resources[i];
            if (resource.size == 0 || (resource.kind != RGResourceKind::Buffer && resource.kind != RGResourceKind::Texture))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            auto& segments = states[i];
            segments.push_back({ .end = resource.size });
            for (const auto& range : resource.initialized) {
                if (range.size == 0 || !IsValidBufferRange(resource.size, range.offset, range.size))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                split(segments, range.offset);
                split(segments, range.offset + range.size);
                for (auto& segment : segments)
                    if (segment.begin >= range.offset && segment.end <= range.offset + range.size) segment.initialized = true;
            }
        }
        RGCompiledSchedule result;
        const auto addEdge = [&](uint32_t before, uint32_t after, RGDependencyKind kind) {
            if (before == noPass || before == after) return;
            const RGDependency edge{ .before = before, .after = after, .kind = kind };
            if (std::find(result.dependencies.begin(), result.dependencies.end(), edge) == result.dependencies.end())
                result.dependencies.push_back(edge);
        };
        const auto validAccess = [&](const RGResourceAccess& access) {
            return access.resource < resources.size() && access.size != 0
                && IsValidBufferRange(resources[access.resource].size, access.offset, access.size)
                && (access.mode == RGAccessMode::Read || access.mode == RGAccessMode::Write || access.mode == RGAccessMode::ReadWrite)
                && (access.initialization == RGWriteInitialization::Preserve || access.initialization == RGWriteInitialization::Initialize
                    || access.initialization == RGWriteInitialization::Discard)
                && (access.mode != RGAccessMode::Read || access.initialization == RGWriteInitialization::Preserve);
        };
        for (uint32_t p = 0; p < passes.size(); ++p) {
            const auto& pass = passes[p];
            if (pass.accesses.size() >= noPass)
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
            for (uint32_t a = 0; a < pass.accesses.size(); ++a) {
                const auto& access = pass.accesses[a];
                if (!validAccess(access)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                // 一个 pass 内允许重复只读绑定；重叠写须合并成单项 ReadWrite，禁止虚构 pass 内屏障。
                for (uint32_t previous = 0; previous < a; ++previous) {
                    const auto& prior = pass.accesses[previous];
                    if (access.resource == prior.resource && access.offset < prior.offset + prior.size
                        && prior.offset < access.offset + access.size
                        && (access.mode != RGAccessMode::Read || prior.mode != RGAccessMode::Read))
                        return { .code = RHIErrorCode::InvalidDescription, .operation = "RenderGraphCompiler::OverlappingPassWrites" };
                }
                auto& segments = states[access.resource];
                split(segments, access.offset);
                split(segments, access.offset + access.size);
                const bool writes = access.mode != RGAccessMode::Read;
                // Preserve 的局部写仍保留旧内容，必须保持旧生产者及其初始化证明。
                const bool reads = access.mode != RGAccessMode::Write || access.initialization == RGWriteInitialization::Preserve;
                uint64_t writeVersion = 0;
                if (writes) {
                    if (nextVersion[access.resource] == std::numeric_limits<uint64_t>::max())
                        return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
                    writeVersion = ++nextVersion[access.resource];
                }
                for (auto& segment : segments) {
                    if (segment.begin < access.offset || segment.end > access.offset + access.size) continue;
                    if (reads && !segment.initialized)
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphCompiler::UninitializedRead" };
                    if (reads) addEdge(segment.writer, p, RGDependencyKind::RAW);
                    if (writes) {
                        addEdge(segment.writer, p, RGDependencyKind::WAW);
                        for (const uint32_t reader : segment.readers) addEdge(reader, p, RGDependencyKind::WAR);
                    }
                    result.versions.push_back({ .pass = p, .access = a, .offset = segment.begin, .size = segment.end - segment.begin,
                        .inputVersion = segment.version, .outputVersion = writes ? writeVersion : segment.version });
                    if (writes) {
                        segment.writer = p;
                        segment.version = writeVersion;
                        segment.readers.clear();
                        if (access.initialization == RGWriteInitialization::Initialize) segment.initialized = true;
                        else if (access.initialization == RGWriteInitialization::Discard) segment.initialized = false;
                    } else if (std::find(segment.readers.begin(), segment.readers.end(), p) == segment.readers.end()) {
                        segment.readers.push_back(p);
                    }
                }
            }
        }
        for (const auto& edge : dependencies) {
            if (edge.before >= passes.size() || edge.after >= passes.size() || edge.before == edge.after
                || edge.kind != RGDependencyKind::Explicit)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            addEdge(edge.before, edge.after, RGDependencyKind::Explicit);
        }

        std::vector<uint8_t> live(passes.size(), 0);
        for (size_t p = 0; p < passes.size(); ++p) live[p] = passes[p].sideEffect ? 1 : 0;
        for (const auto& output : exports) {
            if (!validAccess(output) || output.mode != RGAccessMode::Read)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (const auto& segment : states[output.resource]) {
                if (segment.begin >= output.offset + output.size || segment.end <= output.offset) continue;
                if (!segment.initialized)
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphCompiler::UninitializedExport" };
                if (segment.writer != noPass) live[segment.writer] = 1;
            }
        }
        // 只有真实数据来源及显式依赖延续存活；WAR/WAW 是顺序约束，不能复活被全覆盖的旧写者。
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& edge : result.dependencies) {
                if ((edge.kind == RGDependencyKind::RAW || edge.kind == RGDependencyKind::Explicit)
                    && live[edge.after] && !live[edge.before]) {
                    live[edge.before] = 1;
                    changed = true;
                }
            }
        }
        // 全声明图稳定拓扑后只过滤死 pass，保留跨死写者传递的 WAR/WAW 顺序，也不隐藏循环。
        const auto sort = [&](std::vector<uint32_t>& order) {
            std::vector<size_t> indegree(passes.size(), 0);
            std::vector<uint8_t> done(passes.size(), 0);
            for (const auto& edge : result.dependencies) ++indegree[edge.after];
            for (size_t iteration = 0; iteration < passes.size(); ++iteration) {
                uint32_t next = noPass;
                for (uint32_t p = 0; p < passes.size(); ++p)
                    if (!done[p] && indegree[p] == 0) { next = p; break; }
                if (next == noPass) return false;
                done[next] = 1;
                order.push_back(next);
                for (const auto& edge : result.dependencies)
                    if (edge.before == next) --indegree[edge.after];
            }
            return true;
        };
        std::vector<uint32_t> validationOrder;
        if (!sort(validationOrder))
            return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphCompiler::DependencyCycle" };
        for (const uint32_t pass : validationOrder) if (live[pass]) result.passOrder.push_back(pass);
        result.culledPassCount = static_cast<uint32_t>(passes.size() - result.passOrder.size());
        schedule = std::move(result);
        return {};
    }
    catch (const std::bad_alloc&) {
        schedule = {};
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphCompiler::BuildScheduleAllocation" };
    }
    catch (const std::length_error&) {
        schedule = {};
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphCompiler::BuildScheduleCapacity" };
    }

    RHIStatus RenderGraphCompiler::Compile(RenderGraphBuilder& builder, RenderGraphPlan& plan) try
    {
        constexpr const char* operation = "RenderGraphCompiler::Compile";
        auto& m_Id = builder.m_Id;
        auto& m_Compiled = builder.m_Compiled;
        auto& m_Buffers = builder.m_Buffers;
        auto& m_Textures = builder.m_Textures;
        auto& m_Passes = builder.m_Passes;
        auto& m_Present = builder.m_Present;
        using PassKind = RenderGraphBuilder::PassKind;
        plan = RenderGraphPlan{};

        if (m_Id == 0) return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        if (m_Compiled || (m_Present.HasValue() && !builder.Resolve(m_Present))
            || (!m_Present.HasValue() && builder.m_Exports.empty() && builder.m_ReadbackExports.empty() && std::none_of(m_Passes.begin(), m_Passes.end(),
                [](const auto& pass) { return pass.kind == PassKind::TextureReadback; }))) return { .code = RHIErrorCode::InvalidState, .operation = operation };
        if (m_Buffers.size() >= std::numeric_limits<uint32_t>::max()
            || m_Textures.size() >= std::numeric_limits<uint32_t>::max() - m_Buffers.size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        RenderGraphPlan compiled;
        std::vector<RGResourceDeclaration> resources;
        std::vector<RGPassDeclaration> declarations;
        builder.Describe(resources, declarations);
        std::vector<RGResourceAccess> outputs;
        if (m_Present.HasValue()) outputs.push_back({ .resource = static_cast<uint32_t>(m_Buffers.size()) + m_Present.slot.index, .size = 1 });
        for (const auto texture : builder.m_Exports) {
            const auto& snapshot = m_Textures[texture.slot.index].snapshot;
            outputs.push_back({ .resource = static_cast<uint32_t>(m_Buffers.size()) + texture.slot.index,
                .size = uint64_t(snapshot.mipLevels) * snapshot.arrayLayers });
        }
        for (const auto texture : builder.m_ReadbackExports) {
            const auto& snapshot = m_Textures[texture.slot.index].snapshot;
            outputs.push_back({ .resource = static_cast<uint32_t>(m_Buffers.size()) + texture.slot.index,
                .size = uint64_t(snapshot.mipLevels) * snapshot.arrayLayers });
        }
        if (const auto status = BuildSchedule(resources, declarations, builder.m_Dependencies, outputs, compiled.m_Schedule);
            !status.IsSuccess()) return status;
        compiled.m_Queries = builder.m_Queries;

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
        for (const auto& resource : m_Textures) {
            auto state = resource.snapshot.state;
            if (state.initialized && state.initializedSubresources.empty())
                MarkInitialized(state.initializedSubresources, 0, uint64_t(resource.snapshot.mipLevels) * resource.snapshot.arrayLayers);
            textures.push_back(std::move(state));
        }
        const auto fullTextureRange = [&](uint32_t index) {
            RHITextureSubresourceRange range;
            range.levelCount = m_Textures[index].snapshot.mipLevels;
            range.layerCount = m_Textures[index].snapshot.arrayLayers;
            range.aspect = GetPixelFormatInfo(m_Textures[index].snapshot.format).isDepth ? RHITextureAspect::Depth : RHITextureAspect::Color;
            return range;
        };

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
            const auto range = fullTextureRange(index);
            step.textureBarriers.push_back({
                .view = m_Textures[index].handle, .range = range,
                .before = ConservativeTextureBefore(state.access.layout), .after = after
            });
            state.access = after;
            if (loadOp == RHILoadOp::Clear) state.initialized = true;
            else if (loadOp == RHILoadOp::DontCare) state.initialized = false;
            if (storeOp == RHIStoreOp::DontCare) state.initialized = false;
            if (state.initialized) MarkInitialized(state.initializedSubresources, 0, uint64_t(range.levelCount) * range.layerCount);
            else state.initializedSubresources.clear();
            textureUsed[index] = 1;
            return {};
        };

        for (const uint32_t passIndex : compiled.m_Schedule.passOrder) {
            const auto& pass = m_Passes[passIndex];
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
                    .range = fullTextureRange(destination),
                    .before = ConservativeTextureBefore(destinationState.access.layout),
                    .after = destinationAccess
                    });

                destinationState.access = destinationAccess;
                MarkInitialized(destinationState.initializedSubresources,
                    uint64_t(pass.textureCopy.range.baseArrayLayer) * m_Textures[destination].snapshot.mipLevels + pass.textureCopy.range.baseMipLevel, 1);
                destinationState.initialized = RangeContains(destinationState.initializedSubresources, 0,
                    uint64_t(m_Textures[destination].snapshot.mipLevels) * m_Textures[destination].snapshot.arrayLayers);
                textureUsed[destination] = 1;
            }
            else if (pass.kind == PassKind::TextureReadback) {
                const uint32_t source = pass.sourceTexture.slot.index;
                const uint32_t destination = pass.destination.slot.index;
                const auto& region = pass.readbackCopy;
                if (!RangeContains(textures[source].initializedSubresources,
                    uint64_t(region.range.baseArrayLayer) * m_Textures[source].snapshot.mipLevels + region.range.baseMipLevel, 1))
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedReadback" };
                step.kind = RenderGraphPlan::StepKind::TextureReadback;
                step.sourceView = m_Textures[source].handle;
                step.destination = m_Buffers[destination].handle;
                step.readbackCopy = region;
                const RHITextureAccess after{ RHIPipelineStage::Transfer, RHIResourceAccess::TransferRead, RHITextureLayout::TransferSource };
                step.textureBarriers.push_back({ .view = step.sourceView, .range = fullTextureRange(source),
                    .before = ConservativeTextureBefore(textures[source].access.layout), .after = after });
                textures[source].access = after;
                textureUsed[source] = 1;
                addBufferAccess(step, destination, { RHIPipelineStage::Transfer, RHIResourceAccess::TransferWrite });
                MarkInitialized(buffers[destination].initialized, region.bufferOffset, pass.textureBytes);
            }
            else if (pass.kind == PassKind::Clear) {
                const uint32_t color = pass.clearTexture.slot.index;
                step.kind = RenderGraphPlan::StepKind::Clear;
                const bool depthClear = GetPixelFormatInfo(m_Textures[color].snapshot.format).isDepth;
                if (depthClear) step.depth = m_Textures[color].handle;
                else step.color = m_Textures[color].handle;
                step.extent = m_Textures[color].snapshot.extent;
                step.draw.clearColor = pass.clearColor;
                step.draw.clearDepth = pass.draw.clearDepth;
                const RHITextureAccess clearAccess = depthClear
                    ? RHITextureAccess{ RHIPipelineStage::DepthStencilTests, RHIResourceAccess::DepthWrite, RHITextureLayout::DepthAttachment }
                    : RHITextureAccess{ RHIPipelineStage::ColorAttachmentOutput, RHIResourceAccess::ColorWrite, RHITextureLayout::ColorAttachment };
                if (const auto status = addAttachmentAccess(step, color, RHILoadOp::Clear, RHIStoreOp::Store, clearAccess);
                    !status.IsSuccess()) return status;
            }
            else {
                const auto& draw = pass.draw;
                const bool indexed = draw.vertexCount == 0;
                const uint32_t vertices = draw.vertices.slot.index;
                const uint32_t indices = draw.indices.slot.index;
                if (indexed && draw.indexCount != 0 && draw.instanceCount != 0) {
                    const uint64_t indexSize = draw.indexType == RHIIndexType::UInt16 ? 2u : 4u;
                    if (!RangeContains(buffers[indices].initialized, draw.indexOffset + uint64_t(draw.firstIndex) * indexSize,
                        uint64_t(draw.indexCount) * indexSize))
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedIndexInput" };
                }
                if (draw.vertices.HasValue() && (draw.vertexCount != 0 || draw.indexCount != 0) && draw.instanceCount != 0
                    && !RangeContains(buffers[vertices].initialized, draw.vertexOffset, m_Buffers[vertices].snapshot.desc.size - draw.vertexOffset))
                    return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedVertexInput" };
                step.kind = RenderGraphPlan::StepKind::Draw;
                step.draw = draw;
                if (draw.vertices.HasValue()) step.vertices = m_Buffers[vertices].handle;
                if (indexed) step.indices = m_Buffers[indices].handle;
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
                            .view = view, .range = fullTextureRange(sampled), .before = ConservativeTextureBefore(sampledState.access.layout), .after = sampledAccess
                        });
                    }
                    sampledState.access = sampledAccess;
                    textureUsed[sampled] = 1;
                    step.bindings.push_back({ .binding = binding.textureBinding, .type = RHIBindingType::SampledTexture, .texture = view });
                    if (std::none_of(step.bindings.begin(), step.bindings.end(), [&](const auto& entry) { return entry.binding == binding.samplerBinding; }))
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
                if (indexed && vertices == indices) {
                    addBufferAccess(step, vertices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::VertexIndexRead });
                } else {
                    if (draw.vertices.HasValue()) addBufferAccess(step, vertices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::VertexRead });
                    if (indexed) addBufferAccess(step, indices, { .stage = RHIPipelineStage::VertexInput, .access = RHIResourceAccess::IndexRead });
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
            if (pass.kind == PassKind::TextureReadback) {
                RenderGraphPlan::Step host;
                host.name = "Readback host visibility";
                host.kind = RenderGraphPlan::StepKind::Transition;
                addBufferAccess(host, pass.destination.slot.index, { RHIPipelineStage::Host, RHIResourceAccess::HostRead });
                compiled.m_Steps.push_back(std::move(host));
            }
        }

        if (m_Present.HasValue()) {
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
            presentState.initializedSubresources.clear();
            textureUsed[present] = 1;
            compiled.m_Steps.push_back(std::move(presentStep));
        }

        for (const auto resource : builder.m_Exports) {
            const auto index = resource.slot.index;
            auto& state = textures[index];
            if (!state.initialized) return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedExport" };
            RenderGraphPlan::Step exportStep;
            exportStep.name = "Export sampled texture";
            exportStep.kind = RenderGraphPlan::StepKind::Transition;
            const RHITextureAccess after{ RHIPipelineStage::FragmentShader, RHIResourceAccess::SampledRead, RHITextureLayout::ShaderReadOnly };
            exportStep.textureBarriers.push_back({ .view = m_Textures[index].handle, .range = fullTextureRange(index),
                .before = ConservativeTextureBefore(state.access.layout), .after = after });
            state.access = after;
            textureUsed[index] = 1;
            compiled.m_Steps.push_back(std::move(exportStep));
        }

        for (const auto resource : builder.m_ReadbackExports) {
            const auto index = resource.slot.index;
            auto& state = textures[index];
            if (!state.initialized) return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraph::UninitializedReadbackExport" };
            RenderGraphPlan::Step exportStep;
            exportStep.name = "Export readback texture";
            exportStep.kind = RenderGraphPlan::StepKind::Transition;
            const RHITextureAccess after{ RHIPipelineStage::Transfer, RHIResourceAccess::TransferRead, RHITextureLayout::TransferSource };
            exportStep.textureBarriers.push_back({ .view = m_Textures[index].handle, .range = fullTextureRange(index),
                .before = ConservativeTextureBefore(state.access.layout), .after = after });
            state.access = after;
            textureUsed[index] = 1;
            compiled.m_Steps.push_back(std::move(exportStep));
        }

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
    catch (const std::bad_alloc&) {
        plan = {};
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphCompiler::CompileAllocation" };
    }
    catch (const std::length_error&) {
        plan = {};
        return { .code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphCompiler::CompileCapacity" };
    }

}
