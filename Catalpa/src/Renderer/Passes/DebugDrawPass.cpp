#include "catpch.h"
#include "Renderer/Passes/DebugDrawPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
    RHIStatus DebugDrawPass::Add(RenderGraph& graph, RGIndexedDrawDesc draw)
    {
        if (draw.vertexCount == 0 || (draw.vertexCount & 1) != 0 || !draw.vertices.HasValue())
            return { .code = RHIErrorCode::InvalidDescription, .operation = "DebugDrawPass/line list" };
        draw.depthReadOnly = true;
        draw.depthLoadOp = RHILoadOp::Load;
        draw.loadOp = RHILoadOp::Load;
        return graph.AddDrawPass("Debug line overlay", draw);
    }
}
