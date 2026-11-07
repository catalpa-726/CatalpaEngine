#pragma once
#include "ECS/World.h"
#include "Assets/AssetHandle.h"
#include "Navigation/NavMeshAsset.h"

namespace Catalpa {
    struct NavigationAgentComponent {
        AssetHandle<NavMeshAsset> mesh;
        Math::Vec3 destination{};
        float speed = 2, stoppingDistance = 0.1f;
        uint64_t requestRevision = 1;
        bool enabled = true;
        AssetVersionRef<NavMeshAsset> meshVersion;
    };
    template <> struct ComponentTraits<NavigationAgentComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
