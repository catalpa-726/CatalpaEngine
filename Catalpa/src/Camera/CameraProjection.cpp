#include "catpch.h"

#include "Camera/CameraProjection.h"

namespace Catalpa {
	namespace {
		using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;

		/**
		 * @brief 将双精度相机矩阵转换为可表示的单精度矩阵。
		 * 只转换派生结果，拒绝溢出及导致不可逆的下溢，不改写镜头输入。
		 *
		 * @param[in] source 按双精度计算的候选相机矩阵。
		 * @param[out] output 成功时写入单精度矩阵。
		 *
		 * @return 成功时返回正常状态；系数溢出或非零系数下溢时返回 OutOfRange。
		 */
		Math::MathStatus ToFloat(const DMat4& source, Math::Mat4& output) noexcept {
			Math::Mat4 candidate{0.0f};
			constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
			for (int column = 0; column < 4; ++column) {
				for (int row = 0; row < 4; ++row) {
					const double value = source[column][row];
					if (!std::isfinite(value) || std::abs(value) > limit)
						return {Math::MathError::OutOfRange, "Camera matrix exceeds finite float range"};
					candidate[column][row] = static_cast<float>(value);
					if (value != 0.0 && candidate[column][row] == 0.0f)
						return {Math::MathError::OutOfRange, "Camera matrix loses a nonzero coefficient"};
				}
			}
			output = candidate;
			return {};
		}

		/**
		 * @brief 检查单精度相机矩阵与其逆矩阵乘积的逐元素残差。
		 * 逐元素检查最终单精度系数的反向误差。较大的世界坐标平移会放大相互抵消的乘积项，因此容差随乘积的量级调整。位置分辨率另按镜头尺度约束。
		 *
		 * @param[in] left 待检验矩阵乘积的左因子，按 left × right 的顺序相乘。
		 * @param[in] right 待检验矩阵乘积的右因子，预期两者乘积在容差内等于单位矩阵。
		 *
		 * @return 乘积的每个元素均在与运算量级相关的容差内逼近单位矩阵时为 true。
		 */
		bool HasIdentityResidual(const Math::Mat4& left, const Math::Mat4& right) noexcept {
			for (int column = 0; column < 4; ++column) {
				for (int row = 0; row < 4; ++row) {
					double sum = 0.0, magnitude = 0.0;
					for (int k = 0; k < 4; ++k) {
						const double product = double(left[k][row]) * right[column][k];
						sum += product;
						magnitude += std::abs(product);
					}
					const double expected = column == row ? 1.0 : 0.0;
					if (!std::isfinite(sum) || !std::isfinite(magnitude) ||
					    std::abs(sum - expected) > Math::InverseResidualTolerance * std::max(1.0, magnitude))
						return false;
				}
			}
			return true;
		}

		bool IsInversePair(const Math::Mat4& matrix, const Math::Mat4& inverse) noexcept {
			return HasIdentityResidual(matrix, inverse) && HasIdentityResidual(inverse, matrix);
		}

		/**
		 * @brief 计算矩阵的无穷范数，供估算相机投影的条件数。
		 * 仅对不含世界平移的投影估计条件数，避免把改变世界原点误当镜头退化。
		 *
		 * @param[in] matrix 用于估计条件数的矩阵。
		 *
		 * @return 所有行中最大的绝对值行和。
		 */
		double InfinityNorm(const Math::Mat4& matrix) noexcept {
			double norm = 0.0;
			for (int row = 0; row < 4; ++row) {
				double sum = 0.0;
				for (int column = 0; column < 4; ++column)
					sum += std::abs(double(matrix[column][row]));
				norm = std::max(norm, sum);
			}
			return norm;
		}

		/**
		 * @brief 检查单精度世界坐标的分辨率是否满足镜头尺度要求。
		 * 至少保留近面距离的1/1024（正交镜头为可见高度的1/1024）作为世界坐标分辨率。
		 * 这是派生float快照的精度预算；拒绝超预算输入，不移动原点或改写作者坐标。
		 *
		 * @param[in] world 第四列含相机世界位置的刚体变换。
		 * @param[in] lens 提供透视近面距离或正交可见高度的镜头。
		 *
		 * @return 三个平移分量均可满足镜头所需的最小位置分辨率时为 true。
		 */
		bool HasPositionPrecision(const Math::Mat4& world, const CameraLens& lens) noexcept {
			const double scale =
			    lens.projection == ProjectionType::Perspective ? lens.nearPlane : lens.orthographicHeight;
			const double budget = scale / 1024.0;
			for (int axis = 0; axis < 3; ++axis) {
				const float magnitude = std::abs(world[3][axis]);
				const float next = std::nextafter(magnitude, std::numeric_limits<float>::infinity());
				if (!std::isfinite(next) || double(next) - magnitude > budget)
					return false;
			}
			return true;
		}
	}

	Math::MathStatus ValidateCameraLens(const CameraLens& lens) noexcept {
		if (lens.projection != ProjectionType::Perspective && lens.projection != ProjectionType::Orthographic)
			return {Math::MathError::OutOfRange, "Camera projection type is unsupported"};
		if (!std::isfinite(lens.nearPlane) || lens.nearPlane < 0.0f ||
		    (lens.projection == ProjectionType::Perspective && lens.nearPlane == 0.0f))
			return {Math::MathError::OutOfRange, "Camera near distance must be finite and positive for perspective"};
		if (lens.infiniteFar && lens.projection == ProjectionType::Orthographic)
			return {Math::MathError::OutOfRange, "Infinite orthographic depth is not an invertible linear projection"};
		if (!lens.infiniteFar && (!std::isfinite(lens.farPlane) || lens.farPlane <= lens.nearPlane))
			return {Math::MathError::OutOfRange, "Camera far distance must exceed near distance"};
		if (lens.projection == ProjectionType::Perspective) {
			if (!std::isfinite(lens.verticalFovRadians) || lens.verticalFovRadians <= 0.0f ||
			    lens.verticalFovRadians >= Math::Pi)
				return {Math::MathError::OutOfRange, "Perspective vertical FOV must be in (0, pi) radians"};
		} else if (!std::isfinite(lens.orthographicHeight) || lens.orthographicHeight <= 0.0f) {
			return {Math::MathError::OutOfRange, "Orthographic height must be finite and positive"};
		}
		return {};
	}

	Math::MathStatus BuildCameraProjection(
	    const CameraLens& lens, const CameraViewport& viewport, Math::Mat4& projection, Math::Mat4& inverse) noexcept {
		if (&projection == &inverse)
			return {Math::MathError::OutOfRange, "Projection outputs must not alias"};
		if (const auto status = ValidateCameraLens(lens); !status.IsSuccess())
			return status;
		if (viewport.width == 0 || viewport.height == 0)
			return {Math::MathError::OutOfRange, "Camera viewport must have nonzero pixel extent"};
		if (!std::isfinite(viewport.jitterPixelsX) || !std::isfinite(viewport.jitterPixelsY))
			return {Math::MathError::NonFinite, "Camera pixel jitter is not finite"};

		const double aspect = static_cast<double>(viewport.width) / viewport.height;
		const double nearDistance = lens.nearPlane;
		const double farDistance = lens.farPlane;
		const double jitterX = 2.0 * viewport.jitterPixelsX / viewport.width;
		const double jitterY = 2.0 * viewport.jitterPixelsY / viewport.height;
		DMat4 result{0.0}, inverseResult{0.0};
		if (lens.projection == ProjectionType::Perspective) {
			const double y = 1.0 / std::tan(static_cast<double>(lens.verticalFovRadians) * 0.5);
			const double x = y / aspect;
			const double signedY = viewport.flipY ? -y : y;
			/* 相机空间 zCamera = -distance，裁剪空间 wClip = distance，投影深度 depth = -A + B / distance。 */
			const double a = lens.infiniteFar ? (lens.reversedZ ? 0.0 : -1.0)
			                                  : (lens.reversedZ ? nearDistance / (farDistance - nearDistance)
			                                                    : farDistance / (nearDistance - farDistance));
			const double b = lens.infiniteFar
			    ? (lens.reversedZ ? nearDistance : -nearDistance)
			    : (lens.reversedZ ? nearDistance * farDistance / (farDistance - nearDistance)
			                      : nearDistance * farDistance / (nearDistance - farDistance));
			result[0][0] = x;
			result[1][1] = signedY;
			result[2][0] = -jitterX;
			result[2][1] = -jitterY;
			result[2][2] = a;
			result[2][3] = -1.0;
			result[3][2] = b;
			inverseResult[0][0] = 1.0 / x;
			inverseResult[1][1] = 1.0 / signedY;
			inverseResult[3][0] = -jitterX / x;
			inverseResult[3][1] = -jitterY / signedY;
			inverseResult[3][2] = -1.0;
			inverseResult[2][3] = 1.0 / b;
			inverseResult[3][3] = a / b;
		} else {
			const double y = 2.0 / lens.orthographicHeight;
			const double x = y / aspect;
			const double signedY = viewport.flipY ? -y : y;
			const double a = lens.reversedZ ? 1.0 / (farDistance - nearDistance) : 1.0 / (nearDistance - farDistance);
			const double b = lens.reversedZ ? farDistance / (farDistance - nearDistance)
			                                : nearDistance / (nearDistance - farDistance);
			result[0][0] = x;
			result[1][1] = signedY;
			result[2][2] = a;
			result[3][0] = jitterX;
			result[3][1] = jitterY;
			result[3][2] = b;
			result[3][3] = 1.0;
			inverseResult[0][0] = 1.0 / x;
			inverseResult[1][1] = 1.0 / signedY;
			inverseResult[2][2] = 1.0 / a;
			inverseResult[3][0] = -jitterX / x;
			inverseResult[3][1] = -jitterY / signedY;
			inverseResult[3][2] = -b / a;
			inverseResult[3][3] = 1.0;
		}

		Math::Mat4 candidate{1.0f}, candidateInverse{1.0f};
		if (const auto status = ToFloat(result, candidate); !status.IsSuccess())
			return status;
		if (const auto status = ToFloat(inverseResult, candidateInverse); !status.IsSuccess())
			return status;
		const double condition = InfinityNorm(candidate) * InfinityNorm(candidateInverse);
		if (!std::isfinite(condition) || condition > Math::MaximumInverseCondition)
			return {Math::MathError::Singular, "Camera projection exceeds the supported inverse condition limit"};
		if (!IsInversePair(candidate, candidateInverse))
			return {
			    Math::MathError::Singular, "Projection and inverse do not meet the finite precision residual bound"};
		projection = candidate;
		inverse = candidateInverse;
		return {};
	}

	Math::MathStatus BuildCameraMatrices(const Math::Mat4& rigidWorld, const CameraLens& lens,
	    const CameraViewport& viewport, CameraMatrices& output) noexcept {
		Math::TransformTRS pose;
		if (const auto status = Math::DecomposeTRS(rigidWorld, pose); !status.IsSuccess())
			return status;
		for (int axis = 0; axis < 3; ++axis)
			if (std::abs(double(pose.scale[axis]) - 1.0) > Math::RecompositionTolerance)
				return {Math::MathError::NotTRS, "Camera world transform must be rigid with unit positive scale"};

		CameraMatrices candidate;
		candidate.inverseView = rigidWorld;
		if (const auto status = Math::InverseAffine(rigidWorld, candidate.view); !status.IsSuccess())
			return status;
		if (const auto status =
		        BuildCameraProjection(lens, viewport, candidate.projection, candidate.inverseProjection);
		    !status.IsSuccess())
			return status;
		if (!HasPositionPrecision(rigidWorld, lens))
			return {Math::MathError::OutOfRange,
			    "Camera world position exceeds the float resolution budget relative to its lens"};

		/* 列向量约定：VP=P*V，逆为cameraWorld*inverse(P)，只转换派生结果。 */
		if (const auto status = ToFloat(DMat4(candidate.projection) * DMat4(candidate.view), candidate.viewProjection);
		    !status.IsSuccess())
			return status;
		if (const auto status = ToFloat(
		        DMat4(candidate.inverseView) * DMat4(candidate.inverseProjection), candidate.inverseViewProjection);
		    !status.IsSuccess())
			return status;
		if (!IsInversePair(candidate.view, candidate.inverseView) ||
		    !IsInversePair(candidate.viewProjection, candidate.inverseViewProjection))
			return {Math::MathError::Singular, "Camera combined matrices exceed the scaled inverse residual bound"};

		candidate.position = Math::Vec3(rigidWorld[3]);
		candidate.right = Math::Vec3(rigidWorld[0]);
		candidate.up = Math::Vec3(rigidWorld[1]);
		candidate.forward = -Math::Vec3(rigidWorld[2]);
		output = candidate;
		return {};
	}

}
