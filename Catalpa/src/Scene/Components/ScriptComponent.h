#pragma once
#include "Assets/AssetId.h"
#include "Scripting/ScriptValue.h"
#include "ECS/World.h"

namespace Catalpa {
	struct ScriptComponent {
		AssetId script;
		bool enabled = true;
		ScriptValues properties;
		bool operator==(const ScriptComponent&) const = default;
	};
	template <> struct ComponentTraits<ScriptComponent> {
		static CATALPA_API const ComponentType& Get();
	};
}
