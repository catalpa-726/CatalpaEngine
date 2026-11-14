#pragma once
#include "Assets/AssetId.h"
#include "Scene/PersistentEntityId.h"
#include "ECS/World.h"
#include "Core/Memory/ValueMap.h"

namespace Catalpa {
	/* 根实体保留来源版本、局部身份映射与显式覆盖，普通子实体不重复拥有来源资产。 */
	struct PrefabInstanceComponent {
		AssetId source;
		uint64_t sourceVersion = 0;
		ValueMap<PersistentEntityId, PersistentEntityId> entities;
		std::string overrides = "{}";
		bool operator==(const PrefabInstanceComponent&) const = default;
	};
	template <> struct ComponentTraits<PrefabInstanceComponent> {
		static CATALPA_API const ComponentType& Get();
	};
}
