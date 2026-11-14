#include "catpch.h"

#include "UI/FontProfile.h"

#include "Core/Serialization/AuthorJson.h"
namespace Catalpa {
	bool ReadFontProfile(std::string_view text, FontProfile& output, std::string& error) {
		try {
			const auto value = AuthorJson::Parse(text, 65536);
			AuthorJson::Fields(
			    value, {"schemaVersion", "previewText", "previewPixelSize", "previewWidth", "previewHeight"});
			if (AuthorJson::Unsigned(value.at("schemaVersion")) != 1)
				throw std::invalid_argument("Unsupported FontProfiles schema");
			FontProfile result;
			result.previewText = value.at("previewText").get<std::string>();
			const auto size = AuthorJson::Unsigned(value.at("previewPixelSize")),
			           width = AuthorJson::Unsigned(value.at("previewWidth")),
			           height = AuthorJson::Unsigned(value.at("previewHeight"));
			if (result.previewText.empty() || result.previewText.size() > 4096 ||
			    result.previewText.find('\0') != result.previewText.npos || size < 8 || size > 96 || width < 64 ||
			    width > 2048 || height < 64 || height > 2048)
				throw std::invalid_argument("Font preview profile text/size limits rejected");
			result.previewPixelSize = uint32_t(size);
			result.previewWidth = uint32_t(width);
			result.previewHeight = uint32_t(height);
			output = std::move(result);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
