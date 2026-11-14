#include "catpch.h"

#include "TransformMath.h"

namespace Catalpa::Math {
	namespace {

		using DVec3 = glm::vec<3, double, glm::packed_highp>;
		using DVec4 = glm::vec<4, double, glm::packed_highp>;
		using DQuat = glm::qua<double, glm::packed_highp>;
		using DMat3 = glm::mat<3, 3, double, glm::packed_highp>;
		using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;

		bool IsFinite(const Vec3& value) noexcept {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const Quat& value) noexcept {
			return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const Mat4& value) noexcept {
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (!std::isfinite(value[column][row]))
						return false;
			return true;
		}

		/**
		 * @brief 验证矩阵系数有限且底行满足仿射变换约定。
		 * 此模块只处理 affine 变换；投影矩阵不以容差被悄悄改成 affine。
		 *
		 * @param[in] value 待用于仿射运算的矩阵。
		 *
		 * @return 矩阵合法时返回成功，否则返回非有限或非仿射错误。
		 */
		MathStatus ValidateAffine(const Mat4& value) noexcept {
			if (!IsFinite(value))
				return {MathError::NonFinite, "Transform matrix contains NaN or infinity."};
			if (value[0][3] != 0.0f || value[1][3] != 0.0f || value[2][3] != 0.0f || value[3][3] != 1.0f)
				return {MathError::NonAffine, "Transform matrix bottom row must be [0,0,0,1]."};
			return {};
		}

		/**
		 * @brief 将双精度仿射变换结果转换为单精度，并检查转换精度。
		 * 转回 float 前检查范围，调用方只在完整成功后发布候选矩阵。
		 *
		 * @param[in] value 按双精度计算的变换矩阵。
		 * @param[out] output 成功时写入单精度变换矩阵。
		 *
		 * @return 系数均可用单精度表示时返回成功，否则返回 OutOfRange。
		 */
		MathStatus ToFloat(const DMat4& value, Mat4& output) noexcept {
			constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
			Mat4 candidate{0.0f};
			for (int column = 0; column < 4; ++column) {
				double columnMagnitude = 0.0;
				for (int row = 0; row < 4; ++row) {
					if (!std::isfinite(value[column][row]) || std::abs(value[column][row]) > limit)
						return {MathError::OutOfRange, "Transform result exceeds finite float range."};
					columnMagnitude = std::max(columnMagnitude, std::abs(value[column][row]));
					candidate[column][row] = static_cast<float>(value[column][row]);
				}
				/* 不让下溢把整条非零基向量变成零；普通 float 舍入按每列相对误差接受。 */
				for (int row = 0; row < 4; ++row) {
					if (std::abs(static_cast<double>(candidate[column][row]) - value[column][row]) >
					    FloatConversionTolerance * columnMagnitude)
						return {
						    MathError::OutOfRange, "Transform result loses required precision in float conversion."};
				}
			}
			output = candidate;
			return {};
		}

		/**
		 * @brief 计算三阶线性矩阵的无穷范数，供估计逆矩阵的条件数。
		 * 行和范数用于尺度无关的线性条件数估计，不把平移量混入可逆性判据。
		 *
		 * @param[in] value 不含平移的三阶线性矩阵。
		 *
		 * @return 所有行中最大的绝对值行和。
		 */
		double InfinityNorm(const DMat3& value) noexcept {
			double norm = 0.0;
			for (int row = 0; row < 3; ++row) {
				double sum = 0.0;
				for (int column = 0; column < 3; ++column)
					sum += std::abs(value[column][row]);
				norm = std::max(norm, sum);
			}
			return norm;
		}

		/**
		 * @brief 检查仿射变换与候选逆矩阵乘积的逐元素残差。
		 * 对返回 float 矩阵检查双向乘积；逐元素允许对应乘加绝对值和所界定的舍入误差。
		 *
		 * @param[in] left 待检验矩阵乘积的左因子，按 left × right 的顺序相乘。
		 * @param[in] right 待检验矩阵乘积的右因子，预期两者乘积在容差内等于单位矩阵。
		 *
		 * @return 乘积各元素均在尺度相关容差内逼近单位矩阵时为 true。
		 */
		bool HasIdentityResidual(const DMat4& left, const DMat4& right) noexcept {
			for (int column = 0; column < 4; ++column) {
				for (int row = 0; row < 4; ++row) {
					double sum = 0.0;
					double magnitude = 0.0;
					for (int k = 0; k < 4; ++k) {
						const double product = left[k][row] * right[column][k];
						sum += product;
						magnitude += std::abs(product);
					}
					const double expected = column == row ? 1.0 : 0.0;
					if (!std::isfinite(sum) ||
					    std::abs(sum - expected) > InverseResidualTolerance * std::max(1.0, magnitude))
						return false;
				}
			}
			return true;
		}

	}

	MathStatus ValidateTRS(const TransformTRS& transform) noexcept {
		if (!IsFinite(transform.translation) || !IsFinite(transform.rotation) || !IsFinite(transform.scale))
			return {MathError::NonFinite, "TRS contains NaN or infinity."};

		const DQuat rotation{transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z};
		const double squaredNorm = glm::dot(rotation, rotation);
		if (std::abs(squaredNorm - 1.0) > RotationNormTolerance)
			return {MathError::InvalidRotation, "Rotation quaternion must have unit squared norm within tolerance."};
		return {};
	}

	MathStatus ComposeTRS(const TransformTRS& transform, Mat4& output) noexcept {
		if (const MathStatus status = ValidateTRS(transform); !status.IsSuccess())
			return status;

		DQuat rotation{transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z};
		/* 仅消除已验近单位四元数副本的浮点舍入；不修复或回写超容差作者值。 */
		rotation /= std::sqrt(glm::dot(rotation, rotation));
		const DMat3 basis = glm::mat3_cast(rotation);
		DMat4 result{1.0};
		for (int column = 0; column < 3; ++column)
			result[column] = DVec4{basis[column] * static_cast<double>(transform.scale[column]), 0.0};
		result[3] = DVec4{DVec3{transform.translation}, 1.0};
		return ToFloat(result, output);
	}

	MathStatus ComposeWorld(const Mat4& parent, const Mat4& local, Mat4& output) noexcept {
		if (const MathStatus status = ValidateAffine(parent); !status.IsSuccess())
			return status;
		if (const MathStatus status = ValidateAffine(local); !status.IsSuccess())
			return status;
		return ToFloat(DMat4{parent} * DMat4{local}, output);
	}

	MathStatus InverseAffine(const Mat4& transform, Mat4& output) noexcept {
		if (const MathStatus status = ValidateAffine(transform); !status.IsSuccess())
			return status;

		const DMat3 linear{DMat4{transform}};
		const double scale = InfinityNorm(linear);
		if (scale == 0.0)
			return {MathError::Singular, "Transform linear part is zero."};

		/* 先按范数缩放，再估算 ||A||inf * ||inverse(A)||inf；均匀 1e-20 缩放仍得到条件数1。 */
		const DMat3 scaled = linear / scale;
		const double determinant = glm::determinant(scaled);
		if (!std::isfinite(determinant) || determinant == 0.0)
			return {MathError::Singular, "Transform linear part is singular."};

		const DMat3 inverseScaled = glm::inverse(scaled);
		const double condition = InfinityNorm(scaled) * InfinityNorm(inverseScaled);
		if (!std::isfinite(condition) || condition > MaximumInverseCondition)
			return {MathError::Singular, "Transform exceeds the supported inverse condition limit."};

		const DMat3 inverseLinear = inverseScaled / scale;
		DMat4 inverse{1.0};
		for (int column = 0; column < 3; ++column)
			inverse[column] = DVec4{inverseLinear[column], 0.0};
		inverse[3] = DVec4{-(inverseLinear * DVec3{transform[3]}), 1.0};

		Mat4 candidate{1.0f};
		if (const MathStatus status = ToFloat(inverse, candidate); !status.IsSuccess())
			return status;
		if (!HasIdentityResidual(DMat4{transform}, DMat4{candidate}) ||
		    !HasIdentityResidual(DMat4{candidate}, DMat4{transform}))
			return {MathError::Singular, "Float inverse failed the two-sided residual check."};
		output = candidate;
		return {};
	}

	MathStatus DecomposeTRS(const Mat4& transform, TransformTRS& output) noexcept {
		if (const MathStatus status = ValidateAffine(transform); !status.IsSuccess())
			return status;

		DMat3 basis{DMat4{transform}};
		DVec3 scale{0.0};
		for (int column = 0; column < 3; ++column) {
			scale[column] = glm::length(basis[column]);
			if (scale[column] == 0.0)
				return {MathError::Singular, "Zero scale has no unique TRS rotation decomposition."};
			if (scale[column] > static_cast<double>(std::numeric_limits<float>::max()))
				return {MathError::OutOfRange, "Decomposed scale exceeds finite float range."};
			basis[column] /= scale[column];
		}

		if (std::abs(glm::dot(basis[0], basis[1])) > OrthogonalityTolerance ||
		    std::abs(glm::dot(basis[0], basis[2])) > OrthogonalityTolerance ||
		    std::abs(glm::dot(basis[1], basis[2])) > OrthogonalityTolerance)
			return {MathError::NotTRS, "Affine shear cannot be represented by a local TRS."};

		/* 反射的分解不唯一；统一将负号分配给 X，返回等价矩阵而非猜测原始作者分量。 */
		if (glm::determinant(basis) < 0.0) {
			basis[0] = -basis[0];
			scale.x = -scale.x;
		}

		DQuat rotation = glm::quat_cast(basis);
		const double squaredNorm = glm::dot(rotation, rotation);
		if (!std::isfinite(squaredNorm) || squaredNorm == 0.0)
			return {MathError::NotTRS, "Cannot derive a finite rotation from the affine basis."};
		rotation /= std::sqrt(squaredNorm);

		TransformTRS candidate{};
		candidate.translation = Vec3{transform[3]};
		candidate.rotation = Quat{static_cast<float>(rotation.w), static_cast<float>(rotation.x),
		    static_cast<float>(rotation.y), static_cast<float>(rotation.z)};
		candidate.scale = Vec3{scale};
		Mat4 recomposed{1.0f};
		if (const MathStatus status = ComposeTRS(candidate, recomposed); !status.IsSuccess())
			return status;

		/* 每一列按自身尺度比较，避免大平移或另一轴的大缩放掩盖小尺度轴上的 shear。 */
		for (int column = 0; column < 3; ++column) {
			const double tolerance = RecompositionTolerance * std::abs(scale[column]);
			for (int row = 0; row < 3; ++row) {
				if (std::abs(static_cast<double>(recomposed[column][row]) - transform[column][row]) > tolerance)
					return {
					    MathError::NotTRS, "TRS recomposition does not preserve the affine basis within tolerance."};
			}
		}
		output = candidate;
		return {};
	}

}
