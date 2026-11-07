#pragma once
#include "Core/Base.h"
#include "Assets/AssetLoadResult.h"
#include "ECS/World.h"
namespace Catalpa {
    CATALPA_API bool BindPhysicsNavigationTerrainVersions(World&, EntityId,
                                                          std::span<const AssetDependencyValue>,
                                                          std::string&);
    CATALPA_API bool CollectPhysicsNavigationTerrainVersions(const World&, EntityId,
                                                             std::vector<AssetDependencyValue>&,
                                                             std::string&);
} // namespace Catalpa
