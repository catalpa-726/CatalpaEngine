#pragma once

#include "Core/Math/MathTypes.h"
#include "ECS/World.h"

namespace Catalpa {

    // local为作者TRS；worldMatrix由TransformSystem统一发布，可包含shear。
    struct TransformComponent {
        Math::TransformTRS local;
        Math::Mat4 worldMatrix{ 1.0f };
        bool worldValid = false;
    };
    template<> struct ComponentTraits<TransformComponent> {
        [[nodiscard]] static CATALPA_API const ComponentType& Get();
    };

}
