#include "catpch.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include "Materials/MaterialValidation.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    bool MaterialImporter::Definition(const ImportedMaterial& m, AssetId shader, bool preview,
                                      std::string& out, std::string& e) {
        using J = nlohmann::json;
        if (!std::isfinite(m.baseColor[3]) || m.baseColor[3] < 0 || m.baseColor[3] > 1) {
            e = "Source base-color alpha must be in [0,1]";
            return false;
        }
        J parameters =
            J::array({{{"id", "base_color"}, {"type", "float4"}, {"default", m.baseColor}, {"min", 0}}});
        if (!preview) {
            parameters.push_back({{"id", "uv_flip_v"}, {"type", "bool"}, {"default", m.textureFlipV}});
            parameters.push_back(
                {{"id", "metallic"}, {"type", "float"}, {"default", m.metallic}, {"min", 0}, {"max", 1}});
            parameters.push_back(
                {{"id", "roughness"}, {"type", "float"}, {"default", m.roughness}, {"min", 0}, {"max", 1}});
            parameters.push_back(
                {{"id", "emissive"}, {"type", "float3"}, {"default", m.emissive}, {"min", 0}});
            // glTF alphaCutoff 允许大于 1，表示掩膜会丢弃所有片元。
            parameters.push_back(
                {{"id", "alpha_cutoff"}, {"type", "float"}, {"default", m.alphaCutoff}, {"min", 0}});
            parameters.push_back({{"id", "normal_scale"}, {"type", "float"}, {"default", m.normalScale}});
            parameters.push_back({{"id", "occlusion_strength"},
                                  {"type", "float"},
                                  {"default", m.occlusionStrength},
                                  {"min", 0},
                                  {"max", 1}});
            for (const auto& [semantic, id] : m.textures) {
                const bool known = semantic == "baseColor" || semantic == "normal" ||
                                   semantic == "emissive" || semantic == "occlusion" ||
                                   semantic == "metallicRoughness";
                parameters.push_back({{"id", semantic + "_texture"},
                                      {"type", "texture2d"},
                                      {"default", id.ToString()},
                                      {"textureSemantic", known ? semantic : std::string("data")},
                                      {"allowTextureFallback", false}});
            }
        }
        J document{{"schemaVersion", 1},
                   {"kind", "definition"},
                   {"family", preview   ? "catalpa.unlit"
                              : m.unlit ? "catalpa.imported_unlit"
                                        : "catalpa.imported_pbr"},
                   {"shadingModel", preview || m.unlit ? "unlit" : "pbr"},
                   {"domain", "surface"},
                   {"alphaMode", preview ? "opaque" : m.alphaMode},
                   {"shader", shader.ToString()},
                   {"vertexEntry", "vertexMain"},
                   {"fragmentEntry", "fragmentMain"},
                   {"parameterBlock", "Material"},
                   {"parameters", parameters},
                   {"renderState",
                    {{"cull", preview || m.doubleSided ? "none" : "back"},
                     {"depthTest", true},
                     {"depthWrite", preview || m.alphaMode != "blend"},
                     {"blend", !preview && m.alphaMode == "blend" ? "alpha" : "opaque"}}},
                   {"allowedStateOverrides", J::array()}};
        const auto text = document.dump();
        std::shared_ptr<const MaterialDefinition> valid;
        if (!ParseMaterialDefinition(text, valid, e))
            return false;
        out = text;
        return true;
    }
}
