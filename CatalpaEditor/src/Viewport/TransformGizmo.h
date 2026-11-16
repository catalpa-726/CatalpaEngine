#pragma once
#include "Application/EditorSelection.h"
#include "Core/Math/TransformMath.h"
#include "Renderer/RenderView.h"
#include "Panels/PanelText.h"
#include "Commands/TransformDrag.h"

namespace Catalpa::Editor {
	struct ViewportSettings;

	/**
	 * @brief 根据本次拖动开始时的大小，在局部轴上按百分比量化缩放。
	 * @param[in] startLocal 带符号的起始局部缩放。
	 * @param[in] startWorld 拖动起点的世界矩阵。
	 * @param[in] draggedWorld 操纵器按连续输入给出的世界矩阵。
	 * @param[in] percent 每一档占起始大小的百分比。
	 * @param[out] result 成功时写入局部缩放，保持未拖动轴和各轴符号。
	 * @return 无法计算有效倍率或结果超出浮点范围时返回失败。
	 */
	Math::MathStatus SnapGizmoScale(const Math::Vec3& startLocal, const Math::Mat4& startWorld,
	    const Math::Mat4& draggedWorld, float percent, Math::Vec3& result);

	/* ImGuizmo 处理屏幕操作，TransformDrag 管理实体实时预览和一次性撤销。 */
	class TransformGizmo {
	public:
		/**
		 * @brief 绘制变换工具、坐标空间与吸附选项。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[in,out] settings 当前工作区的吸附开关和三种操作步长。
		 */
		void Toolbar(const PanelText& text, ViewportSettings& settings);
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
		 * @param[in] settings 工作区为当前工具选定的吸附配置。
		 *
		 * @return 操纵器占用本帧指针交互时为 true，用于抑制视口拾取；拖动失败也可能占用交互。
		 */
		bool Draw(SceneDocument&, const EditorSelection&, const RenderView&, float x, float y, float w, float h,
		    std::string& error, const ViewportSettings& settings);

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
		bool m_WaitRelease = false;
		Math::Vec3 m_StartLocalScale{1};
		Math::Mat4 m_StartWorld{1};
		TransformDrag m_Drag;
		uint64_t m_TargetGeneration = 0;
		RenderInstanceId m_Camera;
		uint64_t m_CameraRevision = 0;
		Math::Mat4 m_Preview{1}, m_ParentWorld{1};
	};
}
