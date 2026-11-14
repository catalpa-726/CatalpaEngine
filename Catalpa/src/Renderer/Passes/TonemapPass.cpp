#include "catpch.h"

#include "Renderer/Passes/TonemapPass.h"

#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
	RHIStatus TonemapPass::Add(RenderGraph& graph, RGIndexedDrawDesc draw, float exposureEV) {
		if (!std::isfinite(exposureEV) || exposureEV < -32 || exposureEV > 32)
			return {.code = RHIErrorCode::InvalidDescription, .operation = "TonemapPass/exposure"};
		const std::array<float, 4> parameters{exposureEV, 0, 0, 0};
		const auto bytes = std::as_bytes(std::span{&parameters, 1});
		draw.pushConstants.assign(bytes.begin(), bytes.end());
		draw.firstVertex = 0;
		draw.vertexCount = 3;
		draw.loadOp = RHILoadOp::Clear;
		draw.depth = {};
		return graph.AddDrawPass("HDR exposure and tonemap", draw);
	}
}
