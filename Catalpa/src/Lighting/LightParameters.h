#pragma once

#include "Core/Math/MathTypes.h"
#include "Lighting/LightUnits.h"

namespace Catalpa {

    enum class LightType { Directional = 0, Point = 1, Spot = 2 };

    // color为非负线性RGB；强度单位由type确定，不保存烘焙或GPU对象。
    struct LightParameters {
        LightType type = LightType::Directional;
        Math::Vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 20.0f;
        float innerConeRadians = LightingPi / 8.0f;
        float outerConeRadians = LightingPi / 4.0f;
        bool castShadows = false;
        float shadowBiasMeters = 0.002f;
    };

}
