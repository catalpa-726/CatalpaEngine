#include "catpch.h"
#include "Renderer/Passes/PickingPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {
    RHIStatus PickingPass::Add(RenderGraph& graph, RGTexture ids,
        std::span<const RGIndexedDrawDesc> draws, float clearDepth)
    {
        if (const auto status = graph.AddClearPass("Picking background zero", ids, { 0, 0, 0, 0 }); !status.IsSuccess()) return status;
        bool first = true;
        for (auto draw : draws) {
            draw.color = ids;
            draw.loadOp = RHILoadOp::Load;
            draw.depthLoadOp = first ? RHILoadOp::Clear : RHILoadOp::Load;
            draw.clearDepth = clearDepth;
            draw.depthReadOnly = false;
            if (const auto status = graph.AddDrawPass("Picking nearest surface", draw); !status.IsSuccess()) return status;
            first = false;
        }
        return {};
    }
}
