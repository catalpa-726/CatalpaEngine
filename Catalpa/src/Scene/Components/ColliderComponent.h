#pragma once
#include "ECS/World.h"
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
namespace Catalpa {
	struct ColliderComponent {
		AssetHandle<CollisionShapeAsset> shape;
		AssetHandle<PhysicsMaterialAsset> material;
		bool sensor = false;
		AssetVersionRef<CollisionShapeAsset> shapeVersion;
		AssetVersionRef<PhysicsMaterialAsset> materialVersion;
	};
	template <> struct ComponentTraits<ColliderComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
