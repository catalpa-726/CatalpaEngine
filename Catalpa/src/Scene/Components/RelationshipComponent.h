#pragma once

#include "ECS/World.h"

namespace Catalpa {

	/* 唯一权威父关系；children在安全点由快照推导，避免维护两份可冲突的关系。 */
	struct RelationshipComponent {
		EntityId parent;
	};
	template <> struct ComponentTraits<RelationshipComponent> {
		[[nodiscard]] static CATALPA_API const ComponentType& Get();
	};

}
