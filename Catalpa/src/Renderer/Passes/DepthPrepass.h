#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraph;

	/* 同一深度目标的opaque预通道；后续Mask/形变必须与Forward共享同一顶点/可见性契约。 */
	class DepthPrepass {
	public:
		/**
		 * @brief 仅对引擎生成的顶点着色器副本添加 Position 的 Invariant 装饰，以满足跨管线深度预通道使用 Equal 比较的要求。
		 * 支持直接输出和块成员；保留原件、标识和运算指令，失败时保留已有输出。
		 * 这一步只处理约定的产物形态；完整 SPIR-V 验证及 S04.3 的着色器元数据检查仍需单独执行。
		 *
		 * @param[in] source 待校验的 SPIR-V 模块，按 32 位字提供。
		 * @param[out] output 成功时接收包含顶点位置不变性装饰的 SPIR-V 模块，供深度及颜色绘制复用。
		 *
		 * @return 返回 SPIR-V 位置输出核对及派生模块构建状态，失败时保留原输出。
		 */
		[[nodiscard]] static CATALPA_API RHIStatus PrepareInvariantVertexShader(
		    std::span<const uint32_t> source, std::vector<uint32_t>& output);

		/**
		 * @brief 将深度预通道绘制登记到渲染图，首次绘制清除深度，后续绘制加载已有深度。
		 * 首draw清除、后续加载，全部Store；输入由值复制，不改作者draw或自行提交队列。
		 *
		 * @param[in,out] graph 接收各深度绘制任务的图。
		 * @param[in] draws 当前视图的索引绘制列表，各项须使用同一深度附件。
		 * @param[in] clearDepth 首个深度绘制的清除值，取值范围为 [0,1]。
		 *
		 * @return 返回深度绘制任务的登记结果；提交和 GPU 执行由后续步骤完成。
		 */
		[[nodiscard]] static RHIStatus Add(
		    RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws, float clearDepth);
	};
}
