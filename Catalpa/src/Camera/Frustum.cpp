#include "catpch.h"

#include "Camera/Frustum.h"

#include "Camera/CameraProjection.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 归一化视锥平面，并识别无限远镜头的恒真远面条件。
		 * 输入float系数精确提升至double，行组合与归一化仅生成派生平面。
		 *
		 * @param[in] source 由视图投影矩阵的行组合得到的四个平面系数。
		 * @param[in] absoluteClip 参与行组合的绝对系数之和，用于估计裁剪误差。
		 * @param[in] allowInfinite 当前平面是否必须表示无限远镜头的恒真远面条件。
		 * @param[out] output 成功时接收单位法线及归一化后的平面系数。合法的无限远面会被标记为不参与裁剪。
		 *
		 * @return 得到可用平面或合法的无限远面时返回成功，否则返回退化或数值错误。
		 */
		Math::MathStatus SetPlane(const std::array<double, 4>& source, const std::array<double, 4>& absoluteClip,
		    bool allowInfinite, FrustumPlane& output) noexcept {
			const double length = std::hypot(source[0], source[1], source[2]);
			if (!std::isfinite(length))
				return {Math::MathError::NonFinite, "Frustum plane normal is not finite"};
			if (length == 0.0) {
				if (!allowInfinite || source[3] <= 0.0)
					return {Math::MathError::Singular, "Frustum has an invalid or unexpectedly degenerate plane"};
				output = {};
				return {};
			}
			/* infiniteFar必须由实际VP的恒真裁剪条件表示，不能静默丢弃一个仍在裁剪的面。 */
			if (allowInfinite)
				return {Math::MathError::OutOfRange, "Infinite far lens disagrees with its final view projection"};
			FrustumPlane candidate;
			candidate.active = true;
			for (size_t i = 0; i < candidate.coefficients.size(); ++i) {
				candidate.coefficients[i] = source[i] / length;
				candidate.absoluteClipCoefficients[i] =
				    std::max(std::abs(candidate.coefficients[i]), absoluteClip[i] / length);
				if (!std::isfinite(candidate.coefficients[i]) || !std::isfinite(candidate.absoluteClipCoefficients[i]))
					return {Math::MathError::OutOfRange, "Normalized frustum plane exceeds finite double range"};
			}
			output = candidate;
			return {};
		}

		/**
		 * @brief 检查视锥平面系数、归一化误差上界及无效远面的约定。
		 * 公共值可能来自用户构造；默认全inactive或伪造的非单位平面不是一个有效视锥。
		 *
		 * @param[in] frustum 待参与包围盒测试的世界空间视锥。
		 *
		 * @return 所有平面均合法时返回成功，否则返回首个检查错误。
		 */
		Math::MathStatus ValidateFrustum(const Frustum& frustum) noexcept {
			for (size_t side = 0; side < frustum.planes.size(); ++side) {
				const auto& plane = frustum.planes[side];
				for (const double value : plane.coefficients)
					if (!std::isfinite(value))
						return {Math::MathError::NonFinite, "Frustum contains a nonfinite plane"};
				for (size_t component = 0; component < plane.absoluteClipCoefficients.size(); ++component)
					if (!std::isfinite(plane.absoluteClipCoefficients[component]) ||
					    plane.absoluteClipCoefficients[component] < std::abs(plane.coefficients[component]))
						return {Math::MathError::OutOfRange, "Frustum clip roundoff bounds are invalid"};
				if (!plane.active) {
					if (side != static_cast<size_t>(FrustumSide::Far) ||
					    plane.coefficients != std::array<double, 4>{} ||
					    plane.absoluteClipCoefficients != std::array<double, 4>{})
						return {Math::MathError::OutOfRange, "Only a canonical infinite far plane may be inactive"};
				} else {
					const double length =
					    std::hypot(plane.coefficients[0], plane.coefficients[1], plane.coefficients[2]);
					if (std::abs(length - 1.0) > 32.0 * std::numeric_limits<double>::epsilon())
						return {Math::MathError::OutOfRange, "Frustum plane normal must have unit length"};
				}
			}
			return {};
		}
	}

	Math::MathStatus BuildFrustum(const Math::Mat4& viewProjection, const CameraLens& lens, Frustum& output) noexcept {
		if (const auto status = ValidateCameraLens(lens); !status.IsSuccess())
			return status;
		using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;
		std::array<std::array<double, 4>, 4> rows{};
		for (int column = 0; column < 4; ++column) {
			for (int row = 0; row < 4; ++row) {
				if (!std::isfinite(viewProjection[column][row]))
					return {Math::MathError::NonFinite, "Frustum view projection is not finite"};
				rows[row][column] = viewProjection[column][row];
			}
		}
		/* float四次积在double范围内；不以固定det阈值误拒绝整体缩放后的等价齐次矩阵。 */
		const double determinant = glm::determinant(DMat4(viewProjection));
		if (!std::isfinite(determinant) || determinant == 0.0)
			return {Math::MathError::Singular, "Frustum view projection is singular"};
		std::array<std::array<double, 4>, 6> coefficients{};
		std::array<std::array<double, 4>, 6> absoluteClip{};
		for (size_t column = 0; column < 4; ++column) {
			coefficients[0][column] = rows[3][column] + rows[0][column];
			coefficients[1][column] = rows[3][column] - rows[0][column];
			coefficients[2][column] = rows[3][column] + rows[1][column];
			coefficients[3][column] = rows[3][column] - rows[1][column];
			coefficients[4][column] = lens.reversedZ ? rows[3][column] - rows[2][column] : rows[2][column];
			coefficients[5][column] = lens.reversedZ ? rows[2][column] : rows[3][column] - rows[2][column];
			absoluteClip[0][column] = absoluteClip[1][column] = std::abs(rows[3][column]) + std::abs(rows[0][column]);
			absoluteClip[2][column] = absoluteClip[3][column] = std::abs(rows[3][column]) + std::abs(rows[1][column]);
			const double depthCombined = std::abs(rows[3][column]) + std::abs(rows[2][column]);
			absoluteClip[4][column] = lens.reversedZ ? depthCombined : std::abs(rows[2][column]);
			absoluteClip[5][column] = lens.reversedZ ? std::abs(rows[2][column]) : depthCombined;
		}
		Frustum candidate;
		for (size_t side = 0; side < candidate.planes.size(); ++side)
			if (const auto status = SetPlane(
			        coefficients[side], absoluteClip[side], side == 5 && lens.infiniteFar, candidate.planes[side]);
			    !status.IsSuccess())
				return status;
		output = candidate;
		return {};
	}

	Math::MathStatus IntersectsFrustum(const Frustum& frustum, const Aabb& bounds, bool& intersects) noexcept {
		if (const auto status = ValidateFrustum(frustum); !status.IsSuccess())
			return status;
		if (const auto status = ValidateBounds(bounds); !status.IsSuccess())
			return status;
		bool candidate = !bounds.empty;
		if (candidate) {
			for (const auto& plane : frustum.planes) {
				if (!plane.active)
					continue;
				double maximum = plane.coefficients[3];
				double magnitude = std::abs(maximum);
				double clipMagnitude = plane.absoluteClipCoefficients[3];
				for (int axis = 0; axis < 3; ++axis) {
					const double support =
					    plane.coefficients[axis] >= 0.0 ? bounds.maximum[axis] : bounds.minimum[axis];
					const double term = plane.coefficients[axis] * support;
					maximum += term;
					magnitude += std::abs(term);
					clipMagnitude += plane.absoluteClipCoefficients[axis] *
					    std::max(std::abs(static_cast<double>(bounds.minimum[axis])),
					        std::abs(static_cast<double>(bounds.maximum[axis])));
				}
				/* 误差容限随尺度变化，覆盖归一化、双精度累加和 GPU 单精度点积的舍入误差，保留原始包围盒的尺寸。 */
				const double margin = clipMagnitude * (16.0 * std::numeric_limits<float>::epsilon()) +
				    magnitude * (32.0 * std::numeric_limits<double>::epsilon());
				if (maximum < -margin) {
					candidate = false;
					break;
				}
			}
		}
		intersects = candidate;
		return {};
	}
}
