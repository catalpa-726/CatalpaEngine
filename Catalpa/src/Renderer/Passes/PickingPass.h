#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraph;
	/* R32_UInt输出一基实例token；Mask共用cutoff，Blend使用统一的0.1 alpha阈值并写深度。 */
	class PickingPass {
	public:
		/**
		 * @brief 清除背景 ID 后登记离相机最近的可拾取表面。
		 *
		 * @param[out] graph 接收拾取任务的图。
		 * @param[in] ids 存放实例标记的整型颜色附件。
		 * @param[in] draws 写入实例标记的索引绘制。
		 * @param[in] clearDepth 与当前投影一致的深度清除值。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(
		    RenderGraph&, RGTexture ids, std::span<const RGIndexedDrawDesc> draws, float clearDepth);
	};
}
