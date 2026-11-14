#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraph;
	/* 首版只为一个方向光生成阴影贴图，点光源和聚光灯仍参与照明。阴影所需资源受既定预算限制。 */
	class ShadowPass {
	public:
		[[nodiscard]] static RHIStatus Add(RenderGraph&, std::span<const RGIndexedDrawDesc> draws);
	};
}
