#pragma once
#include "Core/Math/MathTypes.h"
#include "Audio/AudioClipAsset.h"
namespace Catalpa {
	struct AudioVoiceHandle {
		uint64_t value = 0;
		bool HasValue() const noexcept { return value != 0; }
		auto operator<=>(const AudioVoiceHandle&) const = default;
	};
	struct AudioVoiceSettings {
		float gain = 1, pitch = 1, minDistance = 1, maxDistance = 100;
		bool loop = false, spatial = false;
		Math::Vec3 position{0};
	};
	struct AudioListenerState {
		Math::Vec3 position{0}, forward{0, 0, -1}, up{0, 1, 0};
	};
	struct AudioVoiceSnapshot {
		AudioVoiceHandle handle;
		uint64_t cursor = 0, frames = 0;
		bool playing = false;
		float gain = 1;
	};
	struct AudioAcknowledgement {
		uint64_t sequence = 0;
		AudioVoiceHandle handle;
		bool success = false;
		std::string error;
	};
	struct AudioServiceConfig {
		size_t maxVoices = 64, maxCommands = 256;
		bool allowNullFallback = true, forceNull = false;
	};
} /* 命名空间 Catalpa */
