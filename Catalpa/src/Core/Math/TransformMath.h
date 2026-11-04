#pragma once

#include "Core/Base.h"
#include "MathTypes.h"

namespace Catalpa::Math {

    // 数学错误独立于图形后端；调用方决定记录或向作者返回诊断。
    enum class MathError { None, NonFinite, InvalidRotation, NonAffine, Singular, NotTRS, OutOfRange };

    // 错误消息为静态字符串，状态对象不分配内存，失败由调用方处理。
    struct MathStatus {
        MathError code = MathError::None;
        const char* message = "";
        [[nodiscard]] constexpr bool IsSuccess() const noexcept { return code == MathError::None; }
    };

    // 明确的数值策略：四元数平方范数容差、shear/重组容差、逆的线性条件数上界。
    inline constexpr double RotationNormTolerance = 1.0e-5;
    inline constexpr double OrthogonalityTolerance = 1.0e-5;
    inline constexpr double RecompositionTolerance = 4.0e-5;
    inline constexpr double MaximumInverseCondition = 1.0e6;
    inline constexpr double InverseResidualTolerance = 1.0e-5;
    inline constexpr double FloatConversionTolerance = 1.0e-6;

    // 校验有限值及近单位四元数；允许零或负缩放，零缩放不代表可逆。
    [[nodiscard]] CATALPA_API MathStatus ValidateTRS(const TransformTRS& transform) noexcept;

    // 合成 T*R*S；仅计算时将容差内的四元数副本单位化，不修改作者输入。失败不写 output。
    [[nodiscard]] CATALPA_API MathStatus ComposeTRS(const TransformTRS& transform, Mat4& output) noexcept;

    // parent*local 保留完整 affine/shear；要求两者底行为精确 [0,0,0,1]。失败不写 output。
    [[nodiscard]] CATALPA_API MathStatus ComposeWorld(const Mat4& parent, const Mat4& local, Mat4& output) noexcept;

    // double 中间运算、尺度相关条件数及双向残差检查；均匀小尺度不会仅因小 determinant 被拒绝。
    [[nodiscard]] CATALPA_API MathStatus InverseAffine(const Mat4& transform, Mat4& output) noexcept;

    // 仅分解非退化 TRS，拒绝超容差 shear；反射规范为 X 负缩放，零尺度因旋转不唯一而拒绝。
    // 这是派生表示转换，不修改源矩阵；失败不写 output。
    [[nodiscard]] CATALPA_API MathStatus DecomposeTRS(const Mat4& transform, TransformTRS& output) noexcept;

}
