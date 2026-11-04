#pragma once

#include "Camera/CameraLens.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

    // 校验当前镜头分支的有效参数；正交无限远不能形成可逆的线性深度映射。
    [[nodiscard]] CATALPA_API Math::MathStatus ValidateCameraLens(const CameraLens& lens) noexcept;

    // 深度统一为 [0,1]；输出与逆矩阵仅在全部检查成功后一起发布。
    [[nodiscard]] CATALPA_API Math::MathStatus BuildCameraProjection(const CameraLens& lens,
        const CameraViewport& viewport, Math::Mat4& projection, Math::Mat4& inverse) noexcept;

}
