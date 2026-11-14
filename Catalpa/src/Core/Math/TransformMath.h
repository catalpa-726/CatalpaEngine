#pragma once

#include "Core/Base.h"
#include "MathTypes.h"

namespace Catalpa::Math {

	/* 数学错误独立于图形后端；调用方决定记录或向作者返回诊断。 */
	enum class MathError { None, NonFinite, InvalidRotation, NonAffine, Singular, NotTRS, OutOfRange };

	/* 错误消息为静态字符串，状态对象不分配内存，失败由调用方处理。 */
	struct MathStatus {
		MathError code = MathError::None;
		const char* message = "";
		[[nodiscard]] constexpr bool IsSuccess() const noexcept { return code == MathError::None; }
	};

	/* 明确的数值策略：四元数平方范数容差、shear/重组容差、逆的线性条件数上界。 */
	inline constexpr double RotationNormTolerance = 1.0e-5;
	inline constexpr double OrthogonalityTolerance = 1.0e-5;
	inline constexpr double RecompositionTolerance = 4.0e-5;
	inline constexpr double MaximumInverseCondition = 1.0e6;
	inline constexpr double InverseResidualTolerance = 1.0e-5;
	inline constexpr double FloatConversionTolerance = 1.0e-6;

	/**
	 * @brief 校验 TRS 的有限性及旋转四元数的单位长度。
	 * 校验有限值及近单位四元数；允许零或负缩放，零缩放不代表可逆。
	 *
	 * @param[in] transform 待合成的平移、旋转和缩放。
	 *
	 * @return 平移、旋转和缩放均合法时返回成功；零缩放本身可以通过校验。
	 */
	[[nodiscard]] CATALPA_API MathStatus ValidateTRS(const TransformTRS& transform) noexcept;

	/**
	 * @brief 按 T × R × S 顺序合成列向量约定下的局部变换。
	 * 按列向量顺序 T × R × S 合成。仅将通过容差检查的四元数副本归一化，失败保留原输出。
	 *
	 * @param[in] transform 平移、近单位四元数和逐轴缩放。
	 * @param[out] output 成功时写入局部仿射矩阵。
	 *
	 * @return TRS 合法且结果可用单精度表示时返回成功。
	 */
	[[nodiscard]] CATALPA_API MathStatus ComposeTRS(const TransformTRS& transform, Mat4& output) noexcept;

	/**
	 * @brief 计算父世界矩阵与子局部矩阵的乘积，保留可能产生的剪切。
	 * 按 parent × local 相乘以保留层级产生的剪切。两个输入的底行都须为 [0,0,0,1]。
	 *
	 * @param[in] parent 父实体的世界变换矩阵。
	 * @param[in] local 子实体相对父实体的变换矩阵。
	 * @param[out] output 成功时得到子实体的世界变换。
	 *
	 * @return 两输入均为合法仿射矩阵且结果可表示时返回成功。
	 */
	[[nodiscard]] CATALPA_API MathStatus ComposeWorld(const Mat4& parent, const Mat4& local, Mat4& output) noexcept;

	/**
	 * @brief 求仿射矩阵的逆，并检查条件数和双向乘积残差。
	 * 用双精度估算 3×3 线性部分的条件数，避免纯平移或均匀缩小干扰可逆性判断。写出前检查单精度结果的双向残差。
	 *
	 * @param[in] transform 需要求逆的非奇异仿射矩阵。
	 * @param[out] output 成功时写入逆变换。
	 *
	 * @return 单精度逆矩阵可稳定表示时返回成功，否则返回相应的数学错误。
	 */
	[[nodiscard]] CATALPA_API MathStatus InverseAffine(const Mat4& transform, Mat4& output) noexcept;

	/**
	 * @brief 将仿射变换分解为平移、四元数旋转和逐轴缩放。
	 * 零缩放及超容差剪切无法唯一分解为 TRS。反射统一计入 X 轴缩放，按列检查重组误差后才写出。
	 *
	 * @param[in] transform 待分解的非退化矩阵。
	 * @param[out] output 成功时接收 TRS；若存在反射，将负号分配给 X 轴缩放。
	 *
	 * @return 候选 TRS 能在容差内重组原矩阵时返回成功；退化或不可表示的剪切返回错误。
	 */
	[[nodiscard]] CATALPA_API MathStatus DecomposeTRS(const Mat4& transform, TransformTRS& output) noexcept;

}
