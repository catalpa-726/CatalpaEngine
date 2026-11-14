#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"

namespace Catalpa {
	/* 保留 Renderer/上传模块的简明入口；声明、编译、录制分别由专职组件实施。 */
	class RenderGraph : public RenderGraphBuilder {
	public:
		CATALPA_API explicit RenderGraph(RHIDevice& device);

		[[nodiscard]] CATALPA_API RHIStatus Compile(RenderGraphPlan& plan);
	};
}
