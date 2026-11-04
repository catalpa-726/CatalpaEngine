#include "catpch.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {
    RHIStatus ForwardPass::Add(RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws, bool depthPrepass)
    {
        constexpr const char* operation = "ForwardPass::Add";
        for (const auto& draw : draws)
            if (!draw.color.HasValue() || !draw.depth.HasValue() || draw.color == draw.depth
                || draw.color != draws.front().color || draw.depth != draws.front().depth
                || draw.indexCount == 0 || draw.instanceCount == 0
                || !std::isfinite(draw.clearDepth) || draw.clearDepth < 0.0f || draw.clearDepth > 1.0f)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        bool first = true;
        for (const auto& source : draws) {
            auto draw = source;
            draw.loadOp = RHILoadOp::Load;
            draw.storeOp = RHIStoreOp::Store;
            draw.depthLoadOp = depthPrepass || !first ? RHILoadOp::Load : RHILoadOp::Clear;
            draw.depthStoreOp = RHIStoreOp::Store;
            draw.depthReadOnly = depthPrepass;
            if (const auto status = graph.AddDrawPass("Forward opaque", draw); !status.IsSuccess()) return status;
            first = false;
        }
        return {};
    }
}
