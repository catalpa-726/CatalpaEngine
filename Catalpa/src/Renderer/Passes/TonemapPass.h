#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RenderGraph;
	/* 线性HDR场景应用曝光和ACES；输出仍为线性，SRGB附件负责唯一一次输出编码。 */
	class TonemapPass {
	public:
		/**
		 * @brief 将曝光值写入推送常量并登记 HDR 到显示颜色的全屏绘制。
		 *
		 * @param[in] graph 当前渲染图。
		 * @param[in] draw 目标附件、着色器与输入纹理绑定。
		 * @param[in] exposureEV 以 EV 表示的曝光调整，范围为 [-32,32]。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc, float exposureEV);
	};
}
