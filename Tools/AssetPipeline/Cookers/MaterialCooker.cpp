#include "catpch.h"
#include "AssetPipeline/Cookers/MaterialCooker.h"
#include "AssetPipeline/AssetArchiveWriter.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    bool MaterialCooker::Cook(AssetBuildContext& ctx, const ImportedAsset& asset, const AssetRecord& shader,
                              const std::map<AssetId, AssetRecord>& ready,
                              std::vector<AssetBuildProduct>& output, std::string& e) {
        const auto* material = std::get_if<ImportedMaterial>(&asset.value);
        if (!material) {
            e = "Material cooker requires an imported material";
            return false;
        }
        std::vector<AssetBuildProduct> products;
        for (bool preview : {false, true}) {
            std::vector<AssetRecord> deps{shader};
            if (!preview) {
                for (const auto& [semantic, id] : material->textures) {
                    const auto it = ready.find(id);
                    if (it == ready.end() || it->second.type.ToString() != "image") {
                        e = "Material texture '" + semantic + "' requires image " + id.ToString();
                        return false;
                    }
                    if (std::none_of(deps.begin(), deps.end(), [&](const auto& d) { return d.id == id; }))
                        deps.push_back(it->second);
                }
            }
            std::string text;
            if (!MaterialImporter::Definition(*material, shader.id, preview, text, e))
                return false;
            const auto definitionId =
                ctx.Identity(asset.sourceKey + (preview ? "/preview-definition" : "/definition"));
            AssetBuildProduct definition;
            if (!AssetArchiveWriter::Encode(definitionId, "material_definition",
                                            std::as_bytes(std::span(text.data(), text.size())), deps,
                                            definition, e))
                return false;
            nlohmann::json instance{{"schemaVersion", 1},
                                    {"kind", "instance"},
                                    {"definitionId", definitionId.ToString()},
                                    {"definitionVersion", definition.record.version},
                                    {"revision", 1},
                                    {"parameters", nlohmann::json::object()}};
            text = instance.dump();
            // 实例固定定义和继承的纹理版本，着色器版本已由定义持有。
            deps.erase(deps.begin());
            deps.push_back(definition.record);
            AssetBuildProduct value;
            const auto id = preview ? MaterialImporter::PreviewId(ctx, asset.sourceKey) : asset.id;
            if (!AssetArchiveWriter::Encode(id, "material_instance",
                                            std::as_bytes(std::span(text.data(), text.size())), deps, value,
                                            e))
                return false;
            products.push_back(std::move(definition));
            products.push_back(std::move(value));
        }
        output = std::move(products);
        return true;
    }
}
