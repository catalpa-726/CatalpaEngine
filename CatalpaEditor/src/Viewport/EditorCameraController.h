#pragma once

#include "Application/WorkspaceSettings.h"
#include "Camera/CameraLens.h"
#include "Core/Math/TransformMath.h"
#include "Scene/Scene.h"

namespace Catalpa::Editor {
	/* 键盘移动按秒积分；右键视角和滚轮使用本帧累计的像素、刻度增量。 */
	struct EditorCameraInput {
		float right = 0, up = 0, forward = 0;
		float lookX = 0, lookY = 0;
		float seconds = 0;
		float wheel = 0;
		float speedMetresPerSecond = ViewportSettings{}.cameraSpeed;
	};

	/* 工作区持有的独立视角。只在显式复位时读取场景相机，移动过程不修改 World。 */
	class EditorCameraController {
	public:
		/**
		 * @brief 把独立编辑相机恢复至指定的初始视图。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] viewport 计算重置相机取景比例所用的视口像素尺寸。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Reset(const Scene* scene, std::string& error, CameraViewport viewport = {640, 480});
		/**
		 * @brief 根据视口输入更新编辑相机的位置和朝向。
		 *
		 * @param[in] input 输入的 EditorCameraInput。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Navigate(const EditorCameraInput&, std::string& error);
		/**
		 * @brief 设置投影。
		 *
		 * @param[in] projection 编辑相机要切换到的透视或正交投影模式。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetProjection(ProjectionType projection, std::string& error);

		/**
		 * @brief 对齐轴。
		 *
		 * @param[in] axis 所选坐标轴。
		 * @param[in] sign 选择观察点位于对应轴的正侧或负侧，只接受 +1 和 −1。
		 * @param[in] pivot 对齐后相机注视的世界空间位置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool AlignAxis(int axis, float sign, Math::Vec3 pivot, std::string& error);
		/**
		 * @brief 保持朝向并将独立相机取景移到选中对象的包围球。
		 * @param[in] center 世界空间包围球中心，单位为米。
		 * @param[in] radius 世界空间包围球半径，必须有限且为正。
		 * @param[out] error 参数或相机历史无效时接收原因。
		 * @param[in] viewport 当前显示帧的视口，用宽高比决定取景所受的较窄视场限制。
		 * @return 独立视图更新成功时为 true，不修改场景相机。
		 */
		bool Frame(Math::Vec3 center, float radius, std::string& error, CameraViewport viewport = {640, 480});

		const Math::TransformTRS& Pose() const noexcept { return m_Pose; }

		const CameraLens& Lens() const noexcept { return m_Lens; }

		uint64_t HistoryRevision() const noexcept { return m_HistoryRevision; }

	private:
		Math::TransformTRS m_Pose;
		CameraLens m_Lens;
		uint64_t m_HistoryRevision = 1;
	};
}
