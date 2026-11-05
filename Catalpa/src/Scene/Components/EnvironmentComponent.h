#pragma once

#include "Lighting/EnvironmentAsset.h"
#include "Lighting/EnvironmentSettings.h"
#include "Assets/AssetHandle.h"
#include "ECS/World.h"

namespace Catalpa {

    // 场景仅保存稳定资产引用与展示参数；载荷保活和GPU缓存属于消费者。
    struct EnvironmentComponent {
        AssetHandle<EnvironmentAsset> environment;
        EnvironmentSettings settings;
        bool enabled = true;
    };
    template<> struct ComponentTraits<EnvironmentComponent> {
        [[nodiscard]] static CATALPA_API const ComponentType& Get();
    };

}
