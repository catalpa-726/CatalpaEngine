#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RenderGraph;
	/* 已tonemap的线性显示图传到宿主SRGB附件；UI在此之后合成，不受场景曝光污染。 */
	class PresentPass {
	public:
		/**
		 * @brief 登记将线性显示图像合成到呈现目标的全屏三角形。
		 *
		 * @param[out] graph 接收最终合成任务的图。
		 * @param[in] draw 显示图像的采样绑定和目标附件。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
	};
}
