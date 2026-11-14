#include "catpch.h"

#include "Renderer/Passes/TransparentPass.h"

#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {
	RHIStatus TransparentPass::Add(RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws) {
		for (auto draw : draws) {
			if (!draw.color.HasValue() || !draw.depth.HasValue() || draw.indexCount == 0)
				return {.code = RHIErrorCode::InvalidDescription, .operation = "TransparentPass::Add"};
			draw.loadOp = RHILoadOp::Load;
			draw.depthLoadOp = RHILoadOp::Load;
			draw.depthReadOnly = true;
			if (const auto status = graph.AddDrawPass("Transparent back to front", draw); !status.IsSuccess())
				return status;
		}
		return {};
	}
}
