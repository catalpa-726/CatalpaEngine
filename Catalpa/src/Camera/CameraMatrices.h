#pragma once

#include "Core/Math/MathTypes.h"

namespace Catalpa {

    // 当前帧相机值快照，使用列向量 projection*view*world；不借用组件内存。
    // 按 view 管理的前帧历史及离屏目标生命周期在 S03.4 接入。
    struct CameraMatrices {
        Math::Vec3 position{ 0.0f };
        Math::Vec3 right{ 1.0f, 0.0f, 0.0f };
        Math::Vec3 up{ 0.0f, 1.0f, 0.0f };
        Math::Vec3 forward{ 0.0f, 0.0f, -1.0f };
        Math::Mat4 view{ 1.0f };
        Math::Mat4 projection{ 1.0f };
        Math::Mat4 viewProjection{ 1.0f };
        Math::Mat4 inverseView{ 1.0f };
        Math::Mat4 inverseProjection{ 1.0f };
        Math::Mat4 inverseViewProjection{ 1.0f };
    };

}
