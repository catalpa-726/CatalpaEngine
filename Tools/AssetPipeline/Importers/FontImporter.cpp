#include "catpch.h"
#include "AssetPipeline/Importers/FontImporter.h"
#include "UI/FontAssetLoader.h"
namespace Catalpa::Tools {
    bool FontImporter::Import(AssetBuildContext& context, AssetImportResult& out, std::string& error) const {
        FontAsset font;
        if (!context.Read(context.request.source, font.bytes, error) ||
            !context.ReserveWorking(font.bytes.size() * 3, error))
            return false;
        font.sourceUri = context.SourceKey();
        std::vector<std::byte> payload;
        if (!FontAssetLoader::Encode(font, payload, error))
            return false;
        const auto id = context.Identity("font");
        AssetImportResult candidate;
        candidate.primary = id;
        candidate.assets.push_back(
            {id, "font", "font", ImportedAuthorProduct{"font", std::move(payload), {}}});
        candidate.mappings.push_back({id, "font", "font", "font"});
        out = std::move(candidate);
        error.clear();
        return true;
    }
} // namespace Catalpa::Tools
