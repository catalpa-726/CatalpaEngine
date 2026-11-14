#pragma once
#include "Assets/AssetId.h"
#include "Scripting/ScriptValue.h"
#include "ECS/World.h"

namespace Catalpa {
	struct BehaviorGraphComponent {
		AssetId graph;
		bool enabled = true;
		ScriptValues variables;
		bool operator==(const BehaviorGraphComponent&) const = default;
	};
	template <> struct ComponentTraits<BehaviorGraphComponent> {
		static CATALPA_API const ComponentType& Get();
	};
}
