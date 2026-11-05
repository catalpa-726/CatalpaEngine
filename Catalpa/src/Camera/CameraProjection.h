#pragma once

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

    // 校验当前镜头分支的有效参数；正交无限远不能形成可逆的线性深度映射。
    [[nodiscard]] CATALPA_API Math::MathStatus ValidateCameraLens(const CameraLens& lens) noexcept;

    // 深度统一为 [0,1]；输出与逆矩阵仅在全部检查成功后一起发布。
    [[nodiscard]] CATALPA_API Math::MathStatus BuildCameraProjection(const CameraLens& lens,
        const CameraViewport& viewport, Math::Mat4& projection, Math::Mat4& inverse) noexcept;

    // 场景与独立相机共用刚性姿态、投影和逆的数值策略；失败不替换已有快照。
    [[nodiscard]] CATALPA_API Math::MathStatus BuildCameraMatrices(const Math::Mat4& rigidWorld,
        const CameraLens& lens, const CameraViewport& viewport, CameraMatrices& output) noexcept;

}
