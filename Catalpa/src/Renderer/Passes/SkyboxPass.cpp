#include "catpch.h"

#include "Renderer/Passes/SkyboxPass.h"

#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
	RHIStatus SkyboxPass::Add(RenderGraph& graph, RGIndexedDrawDesc draw) {
		draw.firstVertex = 0;
		draw.vertexCount = 3;
		draw.loadOp = RHILoadOp::Load;
		draw.depth = {};
		return graph.AddDrawPass("Environment sky HDR", draw);
	}
}
