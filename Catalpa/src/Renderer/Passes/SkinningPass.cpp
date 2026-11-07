#include "catpch.h"
#include "Log/Log.h"
#include "SkinningPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
    RHIStatus SkinningPass::Prepare(RHIDevice& device, ComputePipelineLibrary& library,
                                    const RenderSkinningInput& input, SkinningFrame& frame) {
        std::string error;
        const bool sameBinding = frame.binding.HasValue() && frame.binding.GetId() == input.binding.GetId() &&
                                 frame.binding.GetVersion() == input.binding.GetVersion() &&
                                 frame.binding.Get() == input.binding.Get();
        if (!input.binding.HasValue() || !input.binding.Get()->skeletonVersion.HasValue() ||
            !input.binding.Get()->meshVersion.HasValue() ||
            (!sameBinding && !ValidateMeshSkinBinding(*input.binding.Get(), error)) ||
            input.palette.size() != input.binding.Get()->skeletonVersion.Get()->joints.size())
            return {RHIErrorCode::InvalidDescription, 0, "Skinning/input"};
        if (frame.readback.buffer.HasValue()) {
            RHIBufferStateSnapshot snapshot;
            if (device.GetBufferState(frame.readback.buffer, snapshot).IsSuccess() &&
                snapshot.state.lastUse.HasValue()) {
                uint32_t flags = 0;
                if (device
                        .ReadBuffer(frame.readback.buffer, 0, std::as_writable_bytes(std::span{&flags, 1}),
                                    snapshot.state.lastUse)
                        .IsSuccess()) {
                    if (flags && !frame.reportedFailure)
                        CAT_CORE_WARN(
                            "GPU skinning reported a non-finite or degenerate transform/basis; affected "
                            "values used explicit bind-pose or orthogonal-basis fallback.");
                    frame.reportedFailure = flags != 0;
                }
            }
        }
        if (const auto status = frame.status.Ensure(
                device, 4, RHIBufferUsage::Storage | RHIBufferUsage::TransferSource, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.readback.Ensure(device, 4, RHIBufferUsage::TransferDestination,
                                                      RHIMemoryDomain::Readback);
            !status.IsSuccess())
            return status;
        const uint32_t zero = 0;
        if (const auto status =
                device.WriteBuffer(frame.status.buffer, 0, std::as_bytes(std::span{&zero, 1}));
            !status.IsSuccess())
            return status;
        const auto& influences = input.binding.Get()->influences;
        if (const auto status = library.Acquire("Skinning.slang", frame.pipeline); !status.IsSuccess())
            return status;
        if (const auto status = frame.weights.Ensure(device, influences.size() * sizeof(SkinInfluence),
                                                     RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.palette.Ensure(device, input.palette.size() * sizeof(Math::Mat4),
                                                     RHIBufferUsage::Storage, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.output.Ensure(device, influences.size() * 64,
                                                    RHIBufferUsage::Storage | RHIBufferUsage::Vertex,
                                                    RHIMemoryDomain::Device);
            !status.IsSuccess())
            return status;
        if (!sameBinding)
            if (const auto status =
                    device.WriteBuffer(frame.weights.buffer, 0, std::as_bytes(std::span{influences}));
                !status.IsSuccess())
                return status;
        if (const auto status =
                device.WriteBuffer(frame.palette.buffer, 0, std::as_bytes(std::span{input.palette}));
            !status.IsSuccess())
            return status;
        frame.binding = input.binding;
        return {};
    }
    RHIStatus SkinningPass::Add(RenderGraph& graph, BufferHandle source, uint32_t count,
                                SkinningFrame& frame) {
        RGComputeDesc desc;
        desc.pipeline = frame.pipeline->pipeline;
        desc.groups = {(count + 63) / 64, 1, 1};
        const std::array<uint32_t, 4> parameters{count, uint32_t(frame.palette.bytes / 64), 0, 0};
        const auto bytes = std::as_bytes(std::span{parameters});
        desc.pushConstants.assign(bytes.begin(), bytes.end());
        const std::array handles{source, frame.weights.buffer, frame.palette.buffer, frame.output.buffer};
        const std::array<uint64_t, 4> sizes{uint64_t(count) * 64, frame.weights.bytes, frame.palette.bytes,
                                            frame.output.bytes};
        for (uint32_t binding = 0; binding < handles.size(); ++binding) {
            RGBuffer resource;
            if (const auto status = graph.ImportBuffer(handles[binding], resource); !status.IsSuccess())
                return status;
            desc.storageBuffers.push_back({resource, binding, 0, sizes[binding],
                                           binding == 3 ? RGAccessMode::Write : RGAccessMode::Read,
                                           binding == 3});
        }
        RGBuffer statusBuffer, readback;
        if (const auto result = graph.ImportBuffer(frame.status.buffer, statusBuffer); !result.IsSuccess())
            return result;
        desc.storageBuffers.push_back({statusBuffer, 4, 0, 4, RGAccessMode::ReadWrite, false});
        if (const auto result = graph.AddComputePass("Skinning", desc); !result.IsSuccess())
            return result;
        if (const auto result = graph.ImportBuffer(frame.readback.buffer, readback); !result.IsSuccess())
            return result;
        return graph.AddCopyPass("Skinning diagnostics", statusBuffer, readback, {0, 0, 4});
    }
} // namespace Catalpa
