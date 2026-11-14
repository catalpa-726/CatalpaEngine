#pragma once
#include "Application/EditorSelection.h"
#include "Renderer/Readback/Picking.h"
#include "Renderer/RenderSurface.h"

namespace Catalpa {
	class Application;
}

namespace Catalpa::Editor {
	/* 一个工作区只保留最近的点击请求；离屏资源的读回寿命由 Renderer 跟踪至 GPU 完成。 */
	class PickingController {
	public:
		/**
		 * @brief 按已提交帧中的像素坐标请求异步 GPU 拾取。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] frame 已呈现视口的场景、帧序列与纹理代次，用于识别过期的拾取结果。
		 * @param[in] x 已发布视口画面内的水平像素索引。
		 * @param[in] y 已发布视口画面内的垂直像素索引。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Request(
		    Application&, SceneDocument&, const RenderSurfaceSnapshot&, uint32_t x, uint32_t y, std::string& error);
		/**
		 * @brief 检查拾取回读结果，并解析仍属于目标文档的实体。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] frame 已呈现视口的场景、帧序列与纹理代次，用于识别过期的拾取结果。
		 * @param[in,out] selection 有效回读结果更新的实体选择；命中背景时清除选择。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Update(Application&, SceneDocument*, const RenderSurfaceSnapshot*, EditorSelection&, std::string& error);
		/**
		 * @brief 取消待处理的拾取请求并清除关联状态。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 */
		void Cancel(Application&);

	private:
		uint64_t m_Request = 0, m_Ticket = 0;
		StableId m_Document;
		SceneHandle m_Scene;
		PickingRequest m_Expected;
	};
}
