#pragma once
#include "ECS/World.h"
#include "Terrain/TerrainAsset.h"
namespace Catalpa {
    struct TerrainComponent {
        AssetHandle<TerrainAsset> asset;
        float viewDistance = 250, lodDistance = 50;
        bool enabled = true, collisionEnabled = true;
        AssetVersionRef<TerrainAsset> assetVersion;
        AssetVersionRef<MaterialInstance> materialVersion;
    };
    template <> struct ComponentTraits<TerrainComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
