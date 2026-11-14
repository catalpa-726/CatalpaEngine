#include "catpch.h"

#include "Audio/AudioConfig.h"

#include "Core/Serialization/AuthorJson.h"
namespace Catalpa {
	bool ReadAudioConfig(std::string_view text, AudioServiceConfig& out, std::string& error) {
		try {
			const auto json = AuthorJson::Parse(text, 65536);
			AuthorJson::Fields(json, {"schemaVersion", "maxVoices", "maxCommands", "allowNullFallback", "forceNull"});
			if (AuthorJson::Unsigned(json.at("schemaVersion")) != 1)
				throw std::invalid_argument("Unsupported AudioDefaults schema");
			AudioServiceConfig value;
			value.maxVoices = size_t(AuthorJson::Unsigned(json.at("maxVoices")));
			value.maxCommands = size_t(AuthorJson::Unsigned(json.at("maxCommands")));
			value.allowNullFallback = json.at("allowNullFallback").get<bool>();
			value.forceNull = json.at("forceNull").get<bool>();
			if (value.maxVoices < 1 || value.maxVoices > 256 || value.maxCommands < value.maxVoices ||
			    value.maxCommands > 4096)
				throw std::invalid_argument("Audio voice/command limits rejected");
			out = value;
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
