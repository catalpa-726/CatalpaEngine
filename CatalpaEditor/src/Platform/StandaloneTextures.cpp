#include "Platform/StandaloneTextures.h"

#include "AssetPipeline/Importers/TextureImporter.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Documents/AssetDocument.h"
#include "Documents/SceneDocument.h"
#include "Images/ImageAssetLoader.h"
#include "Scene/SceneSerializer.h"

namespace Catalpa::Editor {
	namespace fs = std::filesystem;
	namespace {
		constexpr size_t MaxPayload = 96 * 1024 * 1024;
		fs::path Folder(const fs::path& document) {
			auto folder = document;
			folder += ".textures";
			return folder;
		}
		bool ReadBytes(const fs::path& path, size_t limit, std::vector<std::byte>& bytes, std::string& error) {
			VirtualFileSystem files;
			VirtualPath locator;
			return files.MountReadOnly("image", path.parent_path(), 0, error) &&
			    VirtualPath::Parse("image:/" + Tools::AssetPathText(path.filename()), locator, error) &&
			    files.ReadFile(locator, limit, bytes, error);
		}
		AssetId LocalId(std::span<const std::byte> payload) {
			return Tools::AssetBuildIdentity("standalone-texture/" + Tools::AssetBuildDigest(payload));
		}
	}

	bool StandaloneTextures::Import(AssetManager& assets, const fs::path& source, ImageSemantic semantic,
	    AssetHandle<ImageAsset>& image, std::string& error) try {
		std::vector<std::byte> bytes, payload;
		Tools::AssetBuildContext context({});
		ImageAsset decoded;
		if (!ReadBytes(source, context.request.settings.maxSourceBytes, bytes, error) ||
		    !Tools::TextureImporter::Decode(context, bytes, Tools::AssetPathText(source.filename()), semantic, decoded, error) ||
		    !ImageAssetLoader::EncodePayload(decoded, payload, error))
			return false;
		const auto id = LocalId(payload);
		if (assets.GetStatus(id) != AssetStatus::Ready &&
		    !assets.PublishBuiltin(id, 1, std::make_shared<const ImageAsset>(std::move(decoded)), payload.size() + sizeof(ImageAsset), error))
			return false;
		m_Images.insert(id);
		image = AssetHandle<ImageAsset>{id};
		return true;
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}

	bool StandaloneTextures::Save(AssetManager& assets, const Document& doc, const fs::path& target,
	    std::string& error) const try {
		if (m_Images.empty())
			return true;
		std::vector<AssetId> needed;
		if (const auto* material = dynamic_cast<const AssetDocument*>(&doc); material && material->Material()) {
			for (const auto& texture : material->Material()->textureVersions)
				if (texture.HasValue())
					needed.push_back(texture.GetId());
		} else if (const auto* scene = dynamic_cast<const SceneDocument*>(&doc); scene && scene->Resolve()) {
			SceneAsset captured;
			if (!SceneSerializer::Capture(*scene->Resolve(), captured, error))
				return false;
			needed = SceneSerializer::Dependencies(captured);
		}
		const auto folder = Folder(target);
		for (const auto id : needed) {
			if (!m_Images.contains(id))
				continue;
			fs::create_directory(folder);
			if (fs::canonical(folder) != fs::canonical(target.parent_path()) / folder.filename()) {
				error = "Texture directory points outside this document directory";
				return false;
			}
			const auto file = folder / (id.ToString() + ".catimage");
			std::vector<std::byte> bytes;
			if (fs::exists(file)) {
				if (!ReadBytes(file, MaxPayload, bytes, error))
					return false;
				if (LocalId(bytes) != id) {
					error = "Existing companion texture has different contents: " + Tools::AssetPathText(file);
					return false;
				}
				continue;
			}
			AssetVersionRef<ImageAsset> value;
			bool fallback = false;
			if (!assets.Acquire(id, value, fallback, error) || fallback ||
			    !ImageAssetLoader::EncodePayload(*value.Get(), bytes, error) || !AtomicFileWriter::Write(file, bytes, error))
				return false;
		}
		return true;
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}

	bool StandaloneTextures::Restore(AssetManager& assets, const fs::path& document, std::span<const AssetId> ids,
	    std::string& error) try {
		const auto folder = Folder(document);
		if (document.empty() || !fs::exists(folder))
			return true;
		if (fs::canonical(folder) != fs::canonical(document.parent_path()) / folder.filename()) {
			error = "Invalid companion texture directory: " + Tools::AssetPathText(folder);
			return false;
		}
		for (const auto id : ids) {
			const auto file = folder / (id.ToString() + ".catimage");
			if (!fs::exists(file) || (m_Images.contains(id) && assets.GetStatus(id) == AssetStatus::Ready))
				continue;
			std::vector<std::byte> bytes;
			if (!ReadBytes(file, MaxPayload, bytes, error))
				return false;
			if (LocalId(bytes) != id) {
				error = "Companion texture content does not match the document: " + Tools::AssetPathText(file);
				return false;
			}
			AssetDecodeContext context;
			context.maxResidentBytes = MaxPayload + 1024 * 1024;
			AssetLoadResult value;
			if (!ImageAssetLoader::DecodePayload(bytes, context, value, error) ||
			    (assets.GetStatus(id) != AssetStatus::Ready && !assets.PublishBuiltin(id, 1, std::move(value), error)))
				return false;
			m_Images.insert(id);
		}
		return true;
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}
}
