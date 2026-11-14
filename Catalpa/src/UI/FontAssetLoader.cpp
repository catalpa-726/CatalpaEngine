#include "catpch.h"

#include "UI/FontAssetLoader.h"

#include "Assets/AssetManager.h"
#include "Core/Serialization/BinaryArchive.h"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Catalpa {
	bool ValidateFontBytes(
	    std::span<const std::byte> bytes, std::string& family, uint32_t& glyphCount, std::string& error) {
		if (bytes.empty() || bytes.size() > 32 * 1024 * 1024) {
			error = "Font byte budget exceeded";
			return false;
		}
		FT_Library library = nullptr;
		if (FT_Init_FreeType(&library)) {
			error = "FreeType initialization failed";
			return false;
		}
		struct LibraryOwner {
			FT_Library value;

			~LibraryOwner() { FT_Done_FreeType(value); }
		} owner{library};
		FT_Face face = nullptr;
		if (FT_New_Memory_Face(
		        library, reinterpret_cast<const FT_Byte*>(bytes.data()), FT_Long(bytes.size()), 0, &face)) {
			error = "Font face is invalid or unsupported";
			return false;
		}
		struct FaceOwner {
			FT_Face value;

			~FaceOwner() { FT_Done_Face(value); }
		} faceOwner{face};
		if (!FT_IS_SCALABLE(face) || face->num_glyphs <= 0 || face->num_glyphs > 1048576 ||
		    FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
			error = "Font requires a scalable Unicode face";
			return false;
		}
		std::string name = face->family_name ? face->family_name : "Unnamed";
		if (name.size() > 1024) {
			error = "Font family name is too long";
			return false;
		}
		family.swap(name);
		glyphCount = uint32_t(face->num_glyphs);
		error.clear();
		return true;
	}

	bool FontAssetLoader::Encode(const FontAsset& asset, std::vector<std::byte>& out, std::string& error) {
		std::string family;
		uint32_t count = 0;
		if (!ValidateFontBytes(asset.bytes, family, count, error))
			return false;
		BinaryWriter writer(33 * 1024 * 1024);
		if (!writer.String(asset.sourceUri) || !writer.U64(asset.bytes.size()) || !writer.Bytes(asset.bytes)) {
			error = writer.Error();
			return false;
		}
		out = writer.Data();
		error.clear();
		return true;
	}

	bool FontAssetLoader::Register(AssetManager& manager, std::string& error) {
		AssetLoader loader;
		if (!AssetType::TryParse("font", loader.type))
			return false;
		loader.cppType = typeid(FontAsset);
		loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& out,
		                    std::string& error) {
			if (c.payloadVersion != 1 || !c.dependencies.empty() || context.cancellation.IsCancellationRequested() ||
			    c.payload.size() > context.maxResidentBytes / 3) {
				error = "Font payload version, dependencies or budget rejected";
				return false;
			}
			BinaryReader reader(c.payload);
			FontAsset asset;
			uint64_t count = 0;
			std::span<const std::byte> bytes;
			if (!reader.String(asset.sourceUri, 32760) || !reader.U64(count) || count > 32 * 1024 * 1024 ||
			    !reader.Bytes(size_t(count), bytes) || !reader.AtEnd() ||
			    !ValidateFontBytes(bytes, asset.family, asset.glyphCount, error))
				return false;
			asset.bytes.assign(bytes.begin(), bytes.end());
			const auto used =
			    sizeof(FontAsset) + asset.bytes.capacity() + asset.family.capacity() + asset.sourceUri.capacity();
			out = MakeAssetLoadResult<FontAsset>(std::make_shared<const FontAsset>(std::move(asset)), used);
			error.clear();
			return true;
		};
		return manager.RegisterLoader(std::move(loader), error);
	}
} /* 命名空间 Catalpa */
