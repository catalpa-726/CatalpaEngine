#include "catpch.h"

#include "UI/UIRuntimeConfig.h"

#include "Core/Serialization/AuthorJson.h"
namespace Catalpa {
	bool ReadUIRuntimeConfig(std::string_view text, UIRuntimeConfig& out, std::string& error) {
		try {
			const auto json = AuthorJson::Parse(text, 65536);
			AuthorJson::Fields(json, {"schemaVersion", "density", "maxDocuments"});
			if (AuthorJson::Unsigned(json.at("schemaVersion")) != 1)
				throw std::invalid_argument("Unsupported UIRuntimeDefaults schema");
			UIRuntimeConfig value;
			value.density = float(AuthorJson::Number(json.at("density")));
			const auto count = AuthorJson::Unsigned(json.at("maxDocuments"));
			if (value.density < .5f || value.density > 4 || count < 1 || count > 16)
				throw std::invalid_argument("UI runtime limits rejected");
			value.maxDocuments = uint32_t(count);
			out = value;
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
