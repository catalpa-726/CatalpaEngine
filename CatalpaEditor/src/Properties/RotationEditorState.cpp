#include "Properties/RotationEditorState.h"

namespace Catalpa::Editor {
	namespace {
		constexpr double Pi = 3.14159265358979323846;
		constexpr double DegreesPerRadian = 180.0 / Pi;

		bool Finite(const std::array<double, 3>& value) {
			return std::all_of(value.begin(), value.end(), [](double angle) { return std::isfinite(angle); });
		}

		double NearAngle(double degrees, double reference) {
			return reference + std::remainder(degrees - std::remainder(reference, 360.0), 360.0);
		}

		std::array<double, 3> NearAngles(std::array<double, 3> angles, const std::array<double, 3>& reference) {
			for (size_t axis = 0; axis < angles.size(); ++axis)
				angles[axis] = NearAngle(angles[axis], reference[axis]);
			return angles;
		}

		double Distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
			return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
		}

		/**
		 * @brief 将旋转四元数分解为属性面板使用的欧拉角。
		 *
		 * @param[in] rotation 实体的局部旋转四元数，按 Rz × Ry × Rx 分解。
		 * @param[in] previous 可选的上一组 XYZ 显示角（度），用于选择连续的欧拉角表示。
		 *
		 * @return 按 X、Y、Z 顺序排列的三个欧拉角，单位为度；有上一组角度时选择与其连续的等效表示。
		 */
		std::array<double, 3> Decompose(const glm::dquat& rotation, const std::array<double, 3>* previous) {
			const auto matrix = glm::mat3_cast(rotation);
			/* GLM 使用 matrix[column][row]；这里只分解局部 Rz * Ry * Rx，不涉及父级或 scale。 */
			const double cosineY = std::hypot(matrix[0][0], matrix[0][1]);
			std::array<double, 3> angles{};
			if (cosineY > 1.e-7) {
				angles = {std::atan2(matrix[1][2], matrix[2][2]) * DegreesPerRadian,
				    std::atan2(-matrix[0][2], cosineY) * DegreesPerRadian,
				    std::atan2(matrix[0][1], matrix[0][0]) * DegreesPerRadian};
				if (previous) {
					/* 两组等价分解都靠近上次显示值，避免穿过 +/-90 度时 X/Z 突跳 180 度。 */
					const auto alternate =
					    NearAngles({angles[0] + 180.0, 180.0 - angles[1], angles[2] + 180.0}, *previous);
					angles = NearAngles(angles, *previous);
					if (Distance(alternate, *previous) < Distance(angles, *previous))
						angles = alternate;
				}
			} else {
				const double sign = matrix[0][2] < 0 ? 1.0 : -1.0;
				const double coupled = std::atan2(sign * matrix[1][0], matrix[1][1]) * DegreesPerRadian;
				/* 奇异点只有 X-sign*Z 可辨识。首次显示令 Z=0；之后选距旧草稿最近的等价解。 */
				angles = {coupled, sign * 90.0, 0.0};
				if (previous) {
					const double residual = std::remainder(coupled -
					        (std::remainder((*previous)[0], 360.0) - sign * std::remainder((*previous)[2], 360.0)),
					    360.0);
					angles = {(*previous)[0] + residual * .5, NearAngle(angles[1], (*previous)[1]),
					    (*previous)[2] - sign * residual * .5};
				}
			}
			return angles;
		}
	}

	bool RotationEditorState::Synchronize(
	    StableId document, EntityReference target, const Math::Quat& rotation, std::string& error) {
		const glm::dquat value{rotation.w, rotation.x, rotation.y, rotation.z};
		const double norm = glm::dot(value, value);
		if (!document.HasValue() || !target.HasValue() || !std::isfinite(norm) || norm <= 0) {
			error = "Cannot edit an invalid rotation or entity";
			return false;
		}
		const bool sameTarget = m_Initialized && m_Document == document && m_Target == target;
		if (sameTarget && (rotation == m_Observed || rotation == -m_Observed)) {
			m_Observed = rotation;
			return true;
		}
		const auto* previous = sameTarget && Finite(m_Degrees) ? &m_Degrees : nullptr;
		m_Degrees = Decompose(value / std::sqrt(norm), previous);
		m_Document = document;
		m_Target = target;
		m_Observed = rotation;
		m_Initialized = true;
		/* 外部编辑/撤销或目标代次变化时，新的控件 ID 使旧 ImGui 文本草稿不能覆盖新姿态。 */
		++m_Revision;
		return true;
	}

	bool RotationEditorState::ToQuaternion(Math::Quat& output, std::string& error) const {
		if (!m_Initialized || !Finite(m_Degrees)) {
			error = "Rotation XYZ angles must be finite";
			return false;
		}
		/* 先以 double 对整圈取余，转换不会将用户保存的多圈显示值强制折回 [-180,180]。 */
		const glm::dvec3 radians{std::remainder(m_Degrees[0], 360.0) / DegreesPerRadian,
		    std::remainder(m_Degrees[1], 360.0) / DegreesPerRadian,
		    std::remainder(m_Degrees[2], 360.0) / DegreesPerRadian};
		const auto value = glm::normalize(glm::dquat(radians));
		auto candidate = glm::normalize(Math::Quat{float(value.w), float(value.x), float(value.y), float(value.z)});
		if (glm::dot(candidate, m_Observed) < 0)
			candidate = -candidate;
		output = candidate;
		error.clear();
		return true;
	}
}
