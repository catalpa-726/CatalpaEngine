#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraph;
	/* 消费按相机深度稳定排序的Blend清单；深度只读，管线明确选择Alpha或Additive。 */
	class TransparentPass {
	public:
		/**
		 * @brief 按输入顺序登记透明绘制，加载原颜色并只读深度。
		 *
		 * @param[out] graph 接收透明任务的渲染图。
		 * @param[in] draws 调用方已经由远到近排列的索引绘制列表。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(RenderGraph&, std::span<const RGIndexedDrawDesc> draws);
	};
}
