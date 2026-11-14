#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RenderGraph;
	/* 深度只读的世界线条叠加；使用真实LineList绘制，Renderer拥有动态帧缓冲。 */
	class DebugDrawPass {
	public:
		/**
		 * @brief 加载颜色与深度附件并叠加调试线段。
		 *
		 * @param[out] graph 接收调试绘制的渲染图。
		 * @param[in] draw 包含成对线段顶点的绘制描述。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
	};
}
