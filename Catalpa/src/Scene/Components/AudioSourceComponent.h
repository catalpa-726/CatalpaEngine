#pragma once
#include "Assets/AssetId.h"
#include "Audio/AudioTypes.h"
#include "ECS/World.h"
namespace Catalpa {
	struct AudioSourceComponent {
		AssetId clip;
		AssetVersionRef<AudioClipAsset> clipVersion;
		AudioVoiceSettings settings;
		bool enabled = true, autoPlay = true;
	};
	template <> struct ComponentTraits<AudioSourceComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
