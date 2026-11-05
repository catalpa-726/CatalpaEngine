#pragma once

#include "Core/Base.h"
#include "Lighting/LightParameters.h"
#include "Lighting/EnvironmentSettings.h"
#include <string>

namespace Catalpa {

    // 从世界变换的局部 -Z 轴派生单位方向；退化或非有限时保留 output。
    [[nodiscard]] bool TryGetWorldLightDirection(const Math::Mat4& world, Math::Vec3& output) noexcept;
    [[nodiscard]] CATALPA_API bool ValidateLightParameters(const LightParameters&, std::string& error);
    [[nodiscard]] CATALPA_API bool ValidateEnvironmentSettings(const EnvironmentSettings&, std::string& error);

}
