#pragma once

#include "Core/Math/MathTypes.h"

namespace Catalpa {

    // 曝光为EV停数；只改变展示，不修改环境源和IBL派生数据。
    struct EnvironmentSettings {
        float intensity = 1.0f;
        float rotationRadians = 0.0f;
        float exposureEV = 0.0f;
        bool drawBackground = true;
        bool fogEnabled = false;
        Math::Vec3 fogColor{0.5f, 0.6f, 0.7f};
        float fogDensityPerMeter = 0.0f;
    };

}
