#include "catpch.h"

#include "AssetPipeline/Importers/UIDocumentImporter.h"

#include "UI/UIDocumentAsset.h"
#include "UI/FontAssetLoader.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa::Tools {
	bool UIDocumentImporter::Import(AssetBuildContext& context, AssetImportResult& output, std::string& error) const {
		try {
			std::vector<std::byte> bytes;
			if (!context.Read(context.request.source, bytes, error))
				return false;
			const auto author = AuthorJson::Parse(
			    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), 1024 * 1024);
			AuthorJson::Fields(author,
			    {"schemaVersion", "document", "files", "inlineFiles", "images", "fonts", "fontSources", "values"});
			if (AuthorJson::Unsigned(author.at("schemaVersion")) != 1)
				throw std::invalid_argument("Unsupported UI document schema");
			UIDocumentAsset asset;
			asset.entry = author.at("document").get<std::string>();
			if (!author.at("files").is_array() || author.at("files").size() > 128)
				throw std::invalid_argument("Invalid UI source file table");
			for (const auto& item : author.at("files")) {
				const auto path = item.get<std::string>();
				if (!IsUIRelativePath(path))
					throw std::invalid_argument("UI resource must use a canonical relative path");
				std::filesystem::path resolved;
				if (!context.Resolve(context.request.source, path, resolved, error) ||
				    !context.Read(resolved, bytes, error))
					return false;
				if (!context.ReserveWorking(bytes.size() * 2, error))
					return false;
				if (!asset.files.emplace(path, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
				        .second)
					throw std::invalid_argument("Duplicate UI source path");
			}
			if (author.contains("inlineFiles")) {
				if (!author.at("inlineFiles").is_object() || author.at("inlineFiles").size() > 128)
					throw std::invalid_argument("Invalid inline UI file table");
				for (auto it = author.at("inlineFiles").begin(); it != author.at("inlineFiles").end(); ++it)
					if (!asset.files.emplace(it.key(), it.value().get<std::string>()).second)
						throw std::invalid_argument("Duplicate inline UI source path");
			}
			for (const auto& key : {"images", "fonts"})
				if (author.contains(key)) {
					const auto& table = author.at(key);
					if (!table.is_object())
						throw std::invalid_argument("UI bindings require an object");
					auto& target = std::string_view(key) == "images" ? asset.imageIds : asset.fontIds;
					for (auto it = table.begin(); it != table.end(); ++it)
						target.emplace(it.key(), AuthorJson::Identity<AssetId>(it.value()));
				}
			if (author.contains("values"))
				asset.values = author.at("values").get<decltype(asset.values)>();
			AssetImportResult result;
			if (author.contains("fontSources")) {
				const auto& table = author.at("fontSources");
				if (!table.is_object() || table.size() > 16)
					throw std::invalid_argument("Invalid UI font source table");
				for (auto it = table.begin(); it != table.end(); ++it) {
					if (!IsUIRelativePath(it.key()) || asset.fontIds.contains(it.key()))
						throw std::invalid_argument("Duplicate or invalid UI font alias");
					std::filesystem::path path;
					FontAsset font;
					if (!context.Resolve(context.request.source, it.value().get<std::string>(), path, error) ||
					    !context.Read(path, font.bytes, error) || !context.ReserveWorking(font.bytes.size() * 3, error))
						return false;
					font.sourceUri = AssetPathText(path);
					std::vector<std::byte> encoded;
					if (!FontAssetLoader::Encode(font, encoded, error))
						return false;
					const auto key = "ui/font/" + it.key();
					const auto id = context.Identity(key);
					asset.fontIds.emplace(it.key(), id);
					result.assets.push_back({id, key, key, ImportedAuthorProduct{"font", std::move(encoded), {}}});
					result.mappings.push_back({id, key, key, "font"});
				}
			}
			std::vector<std::byte> payload;
			if (!UIDocumentLoader::Encode(asset, payload, error))
				return false;
			std::set<AssetId> unique;
			for (const auto* table : {&asset.imageIds, &asset.fontIds})
				for (const auto& [path, id] : *table)
					unique.insert(id);
			std::vector<AssetId> dependencies(unique.begin(), unique.end());
			const auto id = context.Identity("ui_document");
			result.primary = id;
			result.assets.push_back({id, "ui_document", "ui_document",
			    ImportedAuthorProduct{"ui_document", std::move(payload), std::move(dependencies)}});
			result.mappings.push_back({id, "ui_document", "ui_document", "ui_document"});
			output = std::move(result);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa::Tools */
