#pragma once

#include "catpch.h"

#include "Core/Math/CoordinateConvention.h"

namespace Catalpa {

    enum class ProjectionType { Perspective, Orthographic };

    // 作者镜头值：右手/-Z forward，角度弧度、距离米；不包含窗口或后端对象。
    struct CameraLens {
        ProjectionType projection = ProjectionType::Perspective;
        float verticalFovRadians = Math::Pi / 3.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float orthographicHeight = 10.0f;
        bool infiniteFar = false;
        bool reversedZ = false;
        constexpr bool operator==(const CameraLens&) const = default;
    };

    // 每个视图自己的像素范围；jitter 是最终 NDC 正轴上的像素偏移。
    // Vulkan 正高度 viewport 的 +Y 是向下；flipY 只在投影生成处执行一次。
    struct CameraViewport {
        uint32_t width = 1;
        uint32_t height = 1;
        float jitterPixelsX = 0.0f;
        float jitterPixelsY = 0.0f;
        bool flipY = true;
        constexpr bool operator==(const CameraViewport&) const = default;
    };

}
