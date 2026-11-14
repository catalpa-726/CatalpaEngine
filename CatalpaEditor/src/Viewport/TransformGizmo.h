#pragma once
#include "Application/EditorSelection.h"
#include "Renderer/RenderView.h"
#include "Panels/PanelText.h"
#include "Commands/TransformDrag.h"

namespace Catalpa::Editor {
	/* ImGuizmo 处理屏幕操作，TransformDrag 管理实体实时预览和一次性撤销。 */
	class TransformGizmo {
	public:
		/**
		 * @brief 绘制变换工具、坐标空间与吸附选项。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Toolbar(const PanelText& text);
		/**
		 * @brief 在选中实体上显示操纵器并处理当前拖动。
		 *
		 * @param[in,out] doc 用于解析目标实体、预览变换并提交拖动事务的场景文档。
		 * @param[in] selection 当前选中实体的标识，按文档解析并核对其有效性。
		 * @param[in] view 产生当前图像的相机、投影矩阵和目标代次，拖动中用于检测视图切换。
		 * @param[in] x 视口图像左边缘的 ImGui 屏幕坐标。
		 * @param[in] y 视口图像上边缘的 ImGui 屏幕坐标。
		 * @param[in] w 视口图像的显示宽度，与 x 使用同一坐标单位。
		 * @param[in] h 视口图像的显示高度，与 y 使用同一坐标单位。
		 * @param[out] error 变换分解或拖动事务失败时接收诊断信息。
		 *
		 * @return 操纵器占用本帧指针交互时为 true，用于抑制视口拾取；拖动失败也可能占用交互。
		 */
		bool Draw(SceneDocument&, const EditorSelection&, const RenderView&, float x, float y, float w, float h,
		    std::string& error);

		/**
		 * @brief 取消尚未确认的操纵器拖动。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Cancel(std::string& error) {
			if (m_Drag.Active())
				m_WaitRelease = true;
			return m_Drag.Cancel(error);
		}

	private:
		int m_Mode = 0, m_Operation = -1;
		bool m_Snap = false, m_WaitRelease = false;
		float m_Step = .25f;
		TransformDrag m_Drag;
		uint64_t m_TargetGeneration = 0;
		RenderInstanceId m_Camera;
		uint64_t m_CameraRevision = 0;
		Math::Mat4 m_Preview{1}, m_ParentWorld{1};
	};
}
