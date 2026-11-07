#pragma once
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
namespace Catalpa {
    class AssetManager;
    CATALPA_API bool RegisterPhysicsAssetLoaders(AssetManager&, std::string& error);
} // namespace Catalpa
