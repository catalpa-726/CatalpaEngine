#include "catpch.h"
#include "GpuScenePasses.h"
#include "Lighting/LightParameters.h"
namespace Catalpa {
    namespace {
        void Push(RGComputeDesc& desc, std::span<const std::byte> bytes) {
            desc.pushConstants.assign(bytes.begin(), bytes.end());
        }
        RHIStatus Storage(RenderGraph& graph, RGComputeDesc& desc, ComputeBuffer& buffer, uint32_t binding,
                          RGAccessMode mode, bool initialize = false) {
            RGBuffer resource;
            if (const auto status = graph.ImportBuffer(buffer.buffer, resource); !status.IsSuccess())
                return status;
            desc.storageBuffers.push_back({resource, binding, 0, buffer.bytes, mode, initialize});
            return {};
        }
    } // namespace
    RHIStatus GpuScenePasses::Prepare(RHIDevice& device, ComputePipelineLibrary& library,
                                      const RenderFramePacket& packet, const RenderView& view,
                                      Extent2D extent, GpuSceneFrame& frame) {
        if (!device.GetCapabilities().compute) {
            frame.tilesX = (extent.width + 15) / 16;
            frame.tilesY = (extent.height + 15) / 16;
            const auto size = 16 + uint64_t(frame.tilesX) * frame.tilesY * 33 * 4;
            frame.lightListBytes = size;
            if (const auto status =
                    frame.lightLists.Ensure(device, size, RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
                !status.IsSuccess())
                return status;
            std::vector<uint32_t> values(size / 4, 0);
            values[0] = frame.tilesX;
            values[1] = frame.tilesY;
            values[2] = uint32_t(std::min<size_t>(32, packet.lights.size()));
            for (uint32_t tile = 0; tile < frame.tilesX * frame.tilesY; ++tile) {
                values[4 + tile * 33] = values[2];
                for (uint32_t light = 0; light < values[2]; ++light)
                    values[5 + tile * 33 + light] = light;
            }
            return device.WriteBuffer(frame.lightLists.buffer, 0, std::as_bytes(std::span{values}));
        }
        if (extent.width > 8192 || extent.height > 8192)
            return {RHIErrorCode::Unsupported, 0, "GPU scene/resolution budget"};
        for (auto entry : {std::pair{"DepthPyramid.slang", &frame.depthKernel},
                           {"LightCulling.slang", &frame.lightKernel},
                           {"GpuCulling.slang", &frame.cullKernel}})
            if (const auto status = library.Acquire(entry.first, *entry.second); !status.IsSuccess())
                return status;
        frame.levels.clear();
        uint32_t width = extent.width, height = extent.height, bytes = 0;
        while (true) {
            frame.levels.push_back({bytes, width, height});
            const uint64_t next = uint64_t(bytes) + uint64_t(width) * height * 4;
            if (next > 64 * 1024 * 1024)
                return {RHIErrorCode::OutOfMemory, 0, "Depth pyramid/64MiB budget"};
            bytes = uint32_t(next);
            if (width == 1 && height == 1)
                break;
            width = (width + 1) / 2;
            height = (height + 1) / 2;
        }
        const bool newDepth = frame.depthPyramid.bytes != bytes;
        if (const auto status =
                frame.depthPyramid.Ensure(device, bytes, RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (newDepth) { // 单RW buffer的未写mip不可被声明当成已初始化；首次明确填充派生scratch。
            std::vector<std::byte> initial(bytes);
            if (const auto status = device.WriteBuffer(frame.depthPyramid.buffer, 0, initial);
                !status.IsSuccess())
                return status;
        }
        frame.tilesX = (extent.width + 15) / 16;
        frame.tilesY = (extent.height + 15) / 16;
        frame.lightListBytes = 16 + uint64_t(frame.tilesX) * frame.tilesY * 33 * 4;
        // device输出属于本图；Compile用显式生命周期边复用其它surface的同规格临时块。
        frame.lightListsGraph = {};
        if (const auto status =
                frame.lightRects.Ensure(device, 32 * 16, RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        std::array<std::array<float, 4>, 32> rectangles{};
        for (size_t index = 0; index < std::min<size_t>(32, packet.lights.size()); ++index) {
            const auto& light = packet.lights[index];
            auto& rectangle = rectangles[index];
            rectangle = {0, 0, float(extent.width), float(extent.height)};
            if (light.parameters.type == LightType::Directional)
                continue;
            glm::vec2 lo{1e30f}, hi{-1e30f};
            bool crossesEye = false;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const auto position =
                    light.position +
                    Math::Vec3{corner & 1 ? light.parameters.range : -light.parameters.range,
                               corner & 2 ? light.parameters.range : -light.parameters.range,
                               corner & 4 ? light.parameters.range : -light.parameters.range};
                const auto clip = view.matrices.viewProjection * Math::Vec4{position, 1};
                if (clip.w <= 1e-5f) {
                    crossesEye = true;
                    break;
                }
                const glm::vec2 pixel{(clip.x / clip.w * 0.5f + 0.5f) * extent.width,
                                      (clip.y / clip.w * 0.5f + 0.5f) * extent.height};
                lo = glm::min(lo, pixel);
                hi = glm::max(hi, pixel);
            }
            if (!crossesEye)
                rectangle = {lo.x, lo.y, hi.x, hi.y};
        }
        return device.WriteBuffer(frame.lightRects.buffer, 0, std::as_bytes(std::span{rectangles}));
    }
    RHIStatus GpuScenePasses::AddDepth(RenderGraph& graph, RGTexture depth, SamplerHandle sampler,
                                       bool reversedZ, GpuSceneFrame& frame) {
        for (uint32_t level = 0; level < frame.levels.size(); ++level) {
            const auto& output = frame.levels[level];
            const auto& input = frame.levels[level ? level - 1 : 0];
            RGComputeDesc desc;
            desc.pipeline = frame.depthKernel->pipeline;
            desc.groups = {(output.width + 7) / 8, (output.height + 7) / 8, 1};
            const std::array<uint32_t, 8> parameters{input.offset, output.offset,      input.width,
                                                     input.height, output.width,       output.height,
                                                     level,        reversedZ ? 1u : 0u};
            Push(desc, std::as_bytes(std::span{parameters}));
            desc.sampledTextures.push_back({depth, sampler, 0, 1});
            if (const auto status = Storage(graph, desc, frame.depthPyramid, 2, RGAccessMode::ReadWrite);
                !status.IsSuccess())
                return status;
            if (const auto status = graph.AddComputePass("Depth pyramid reduction", desc);
                !status.IsSuccess())
                return status;
        }
        return {};
    }
    RHIStatus GpuScenePasses::AddLights(RenderGraph& graph, uint32_t lights, GpuSceneFrame& frame) {
        RGComputeDesc desc;
        desc.pipeline = frame.lightKernel->pipeline;
        desc.groups = {(frame.tilesX * frame.tilesY + 63) / 64, 1, 1};
        const std::array<uint32_t, 4> parameters{frame.tilesX, frame.tilesY, std::min(lights, 32u), 0};
        Push(desc, std::as_bytes(std::span{parameters}));
        if (const auto status = Storage(graph, desc, frame.lightRects, 0, RGAccessMode::Read);
            !status.IsSuccess())
            return status;
        if (!frame.lightListsGraph.HasValue())
            return {RHIErrorCode::InvalidState, 0, "Light culling/transient declaration missing"};
        desc.storageBuffers.push_back(
            {frame.lightListsGraph, 1, 0, frame.lightListBytes, RGAccessMode::Write, true});
        return graph.AddComputePass("Tiled light culling", desc);
    }
    RHIStatus GpuScenePasses::AddVisibility(RHIDevice& device, RenderGraph& graph,
                                            const RenderFramePacket& packet, const RenderView& view,
                                            bool useDepth, std::vector<RGIndexedDrawDesc>& draws,
                                            GpuSceneFrame& frame) {
        if (draws.empty())
            return {};
        struct alignas(16) Input {
            std::array<float, 4> minimum, maximum;
            std::array<uint32_t, 4> command;
        };
        struct alignas(16) Parameters {
            std::array<float, 16> vp;
            std::array<uint32_t, 4> counts;
            std::array<std::array<uint32_t, 4>, 16> mips;
        };
        std::vector<Input> inputs;
        inputs.reserve(draws.size());
        for (const auto& draw : draws) {
            if (draw.sourceInstance >= packet.instances.size() || draw.instanceCount != 1)
                return {RHIErrorCode::InvalidDescription, 0, "GPU cull/instance mapping"};
            const auto& bounds = packet.instances[draw.sourceInstance].worldBounds;
            inputs.push_back({{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0},
                              {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0},
                              {draw.indexCount, draw.firstIndex, uint32_t(draw.baseVertex), 0}});
        }
        Parameters parameters{};
        for (uint32_t column = 0; column < 4; ++column)
            for (uint32_t row = 0; row < 4; ++row)
                parameters.vp[column * 4 + row] = view.matrices.viewProjection[column][row];
        parameters.counts = {uint32_t(draws.size()), uint32_t(frame.levels.size()),
                             view.lens.reversedZ ? 1u : 0u, useDepth ? 1u : 0u};
        for (size_t level = 0; level < frame.levels.size(); ++level)
            parameters.mips[level] = {frame.levels[level].offset, frame.levels[level].width,
                                      frame.levels[level].height, 0};
        if (const auto status = frame.cullInput.Ensure(device, inputs.size() * sizeof(Input),
                                                       RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.cullParameters.Ensure(device, sizeof(parameters),
                                                            RHIBufferUsage::Uniform, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.indirect.Ensure(device, draws.size() * 20,
                                                      RHIBufferUsage::Storage | RHIBufferUsage::Indirect,
                                                      RHIMemoryDomain::Device);
            !status.IsSuccess())
            return status;
        if (const auto status =
                device.WriteBuffer(frame.cullInput.buffer, 0, std::as_bytes(std::span{inputs}));
            !status.IsSuccess())
            return status;
        if (const auto status =
                device.WriteBuffer(frame.cullParameters.buffer, 0, std::as_bytes(std::span{&parameters, 1}));
            !status.IsSuccess())
            return status;
        RGComputeDesc desc;
        desc.pipeline = frame.cullKernel->pipeline;
        desc.groups = {(uint32_t(draws.size()) + 63) / 64, 1, 1};
        if (const auto status = Storage(graph, desc, frame.cullInput, 0, RGAccessMode::Read);
            !status.IsSuccess())
            return status;
        if (const auto status = Storage(graph, desc, frame.depthPyramid, 1, RGAccessMode::Read);
            !status.IsSuccess())
            return status;
        if (const auto status = Storage(graph, desc, frame.indirect, 2, RGAccessMode::Write, true);
            !status.IsSuccess())
            return status;
        RGBuffer uniform, indirect;
        if (const auto status = graph.ImportBuffer(frame.cullParameters.buffer, uniform); !status.IsSuccess())
            return status;
        desc.uniformBuffers.push_back({uniform, 3, 0, sizeof(parameters), RHIShaderVisibility::Compute});
        if (const auto status = graph.AddComputePass("GPU visibility / indirect commands", desc);
            !status.IsSuccess())
            return status;
        if (const auto status = graph.ImportBuffer(frame.indirect.buffer, indirect); !status.IsSuccess())
            return status;
        for (size_t index = 0; index < draws.size(); ++index) {
            draws[index].indirect = indirect;
            draws[index].indirectOffset = index * 20;
        }
        return {};
    }
} // namespace Catalpa
