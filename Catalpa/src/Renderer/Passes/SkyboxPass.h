#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RenderGraph;
	/* HDR背景在不透明场景之前写入，后续深度不受天空影响。 */
	class SkyboxPass {
	public:
		/**
		 * @brief 登记绘制 HDR 环境背景的全屏三角形。
		 *
		 * @param[out] graph 接收天空背景任务的图。
		 * @param[in] draw 天空着色器、环境纹理绑定及颜色目标。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
	};
}
