#include "catpch.h"

#include "Renderer/Passes/PresentPass.h"

#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
	RHIStatus PresentPass::Add(RenderGraph& graph, RGIndexedDrawDesc draw) {
		draw.firstVertex = 0;
		draw.vertexCount = 3;
		draw.loadOp = RHILoadOp::Load;
		draw.depth = {};
		return graph.AddDrawPass("Present linear display image", draw);
	}
}
