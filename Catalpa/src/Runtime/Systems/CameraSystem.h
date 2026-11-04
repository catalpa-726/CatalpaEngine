#pragma once

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "ECS/World.h"

namespace Catalpa {

    class Scene;

    // 将相机组件及已经更新的刚性世界变换转换为完整相机矩阵值。
    class CATALPA_API CameraSystem {
    public:
        [[nodiscard]] static WorldStatus Build(const Scene& scene, EntityId camera,
            const CameraViewport& viewport, CameraMatrices& output);
    };

}
