#pragma once

#include "catpch.h"

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraph;

	/* 单视图基础颜色通道；不加载资产、不遍历World，也不持有GPU资源。 */
	class ForwardPass {
	public:
		/**
		 * @brief 颜色由前置Clear初始化。已有prepass时只读加载深度，否则首draw清除后逐draw加载/写入。
		 * 同一批全部draw须引用同一color/depth目标；失败后调用者弃图，不能提交部分声明。
		 *
		 * @param[in,out] graph 接收绘制任务的图。
		 * @param[in] draws 共享颜色与深度目标的索引绘制。
		 * @param[in] depthPrepass 是否复用已经填充的只读深度附件。
		 *
		 * @return 返回前向绘制任务的登记结果；登记中途失败时应丢弃该图。
		 */
		[[nodiscard]] static RHIStatus Add(
		    RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws, bool depthPrepass);
	};
}
