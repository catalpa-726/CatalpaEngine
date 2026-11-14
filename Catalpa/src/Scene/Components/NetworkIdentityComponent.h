#pragma once
#include "Core/Id/StableId.h"
#include "ECS/World.h"
namespace Catalpa {
	struct NetworkIdentityComponent {
		StableId identity;
		uint64_t generation = 1;
		uint32_t owner = 1;
		bool enabled = true;
	};
	template <> struct ComponentTraits<NetworkIdentityComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
