#pragma once

#include "catpch.h"

#include "Core/Math/MathTypes.h"
#include "Scene/EntityReference.h"

namespace Catalpa::Editor {
	/*
	 * 检查器编辑局部旋转时使用角度草稿。场景、组件模式和磁盘仍使用四元数。
	 * XYZ 的单位为度，构造顺序与 GLM 的 quat(vec3) 一致，即 R = Rz * Ry * Rx。
	 */
	class RotationEditorState {
	public:
		/**
		 * @brief 以当前四元数更新显示角度，保持编辑时的角度连续性。
		 *
		 * @param[in] document 正在显示旋转控件的文档标识，用于区分编辑状态。
		 * @param[in] target 旋转输入绑定的实体引用。
		 * @param[in] rotation 当前实体的四元数旋转，转换为连续显示的欧拉角。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 提供有效的文档、实体及四元数且显示角度已同步时为 true。
		 */
		bool Synchronize(StableId document, EntityReference target, const Math::Quat& rotation, std::string& error);
		/**
		 * @brief 转换为四元数。
		 *
		 * @param[out] output 接收四元数结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ToQuaternion(Math::Quat& output, std::string& error) const;
		void Accept(const Math::Quat& rotation) noexcept { m_Observed = rotation; }

		std::array<double, 3>& Degrees() noexcept { return m_Degrees; }
		const std::array<double, 3>& Degrees() const noexcept { return m_Degrees; }
		uint64_t Revision() const noexcept { return m_Revision; }

	private:
		StableId m_Document;
		EntityReference m_Target;
		Math::Quat m_Observed{1, 0, 0, 0};
		std::array<double, 3> m_Degrees{};
		uint64_t m_Revision = 0;
		bool m_Initialized = false;
	};
}
