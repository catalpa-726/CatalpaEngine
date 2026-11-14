#include "catpch.h"

#include "AssetPipeline/Importers/MaterialImporter.h"

#include "Materials/MaterialValidation.h"

#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
	bool MaterialImporter::SamplerFlags(
	    uint32_t wrapU, uint32_t wrapV, uint32_t minFilter, uint32_t magFilter, uint32_t& output, std::string& error) {
		const auto wrap = [](uint32_t value) -> int {
			if (value == 10497)
				return 0;
			if (value == 33071)
				return 1;
			if (value == 33648)
				return 2;
			return -1;
		};
		const int u = wrap(wrapU), v = wrap(wrapV);
		if (u < 0 || v < 0 || (magFilter != 9728 && magFilter != 9729)) {
			error = "Unsupported source sampler wrap or magnification filter";
			return false;
		}
		uint32_t flags = uint32_t(u) | (uint32_t(v) << 2) | (magFilter == 9729 ? 32u : 0u);
		switch (minFilter) {
		case 9728:
			break;
		case 9729:
			flags |= 16;
			break;
		case 9984:
			flags |= 128;
			break;
		case 9985:
			flags |= 128 | 16;
			break;
		case 9986:
			flags |= 128 | 64;
			break;
		case 9987:
			flags |= 128 | 64 | 16;
			break;
		default:
			error = "Unsupported source sampler minification filter";
			return false;
		}
		output = flags;
		error.clear();
		return true;
	}

	bool MaterialImporter::Definition(
	    const ImportedMaterial& m, AssetId shader, bool preview, std::string& out, std::string& e) {
		using J = nlohmann::json;
		if (!std::isfinite(m.baseColor[3]) || m.baseColor[3] < 0 || m.baseColor[3] > 1) {
			e = "Source base-color alpha must be in [0,1]";
			return false;
		}
		J parameters = J::array({{{"id", "base_color"}, {"type", "float4"}, {"default", m.baseColor}, {"min", 0}}});
		parameters.push_back({{"id", "uv_scale_offset"}, {"type", "float4"}, {"default", J::array({1, 1, 0, 0})}});
		parameters.push_back(
		    {{"id", "uv_cycles"}, {"type", "float2"}, {"default", J::array({0, 0})}, {"min", -65536}, {"max", 65536}});
		parameters.push_back({{"id", "pulse_cycles"}, {"type", "uint"}, {"default", 0}, {"min", 0}, {"max", 65536}});
		if (!preview) {
			parameters.push_back({{"id", "uv_flip_v"}, {"type", "bool"}, {"default", m.textureFlipV}});
			parameters.push_back(
			    {{"id", "metallic"}, {"type", "float"}, {"default", m.metallic}, {"min", 0}, {"max", 1}});
			parameters.push_back(
			    {{"id", "roughness"}, {"type", "float"}, {"default", m.roughness}, {"min", 0}, {"max", 1}});
			parameters.push_back({{"id", "emissive"}, {"type", "float3"}, {"default", m.emissive}, {"min", 0}});
			/* glTF alphaCutoff 允许大于 1，表示掩膜会丢弃所有片元。 */
			parameters.push_back({{"id", "alpha_cutoff"}, {"type", "float"}, {"default", m.alphaCutoff}, {"min", 0}});
			parameters.push_back({{"id", "normal_scale"}, {"type", "float"}, {"default", m.normalScale}});
			parameters.push_back({{"id", "occlusion_strength"}, {"type", "float"}, {"default", m.occlusionStrength},
			    {"min", 0}, {"max", 1}});
			for (const auto& [semantic, id] : m.textures) {
				const bool known = semantic == "baseColor" || semantic == "normal" || semantic == "emissive" ||
				    semantic == "occlusion" || semantic == "metallicRoughness";
				/* 源格式额外通道仍记录于SourceMap及独立图像资产；普通PBR不虚构height/specular等能力。 */
				if (!known)
					continue;
				const auto sampler = m.samplers.find(semantic);
				parameters.push_back({{"id", semantic + "_sampler"}, {"type", "uint"},
				    {"default", sampler == m.samplers.end() ? 240u : sampler->second}, {"min", 0}, {"max", 255},
				    {"allowOverride", false}});
				parameters.push_back({{"id", semantic + "_texture"}, {"type", "texture2d"}, {"default", id.ToString()},
				    {"textureSemantic", known ? semantic : std::string("data")}, {"allowTextureFallback", false}});
			}
		}
		J document{{"schemaVersion", 1}, {"kind", "definition"},
		    {"family",
		        preview       ? "catalpa.unlit"
		            : m.unlit ? "catalpa.imported_unlit"
		                      : "catalpa.imported_pbr"},
		    {"shadingModel", preview || m.unlit ? "unlit" : "pbr"}, {"domain", "surface"},
		    {"alphaMode", preview ? "opaque" : m.alphaMode}, {"shader", shader.ToString()},
		    {"vertexEntry", "vertexMain"}, {"fragmentEntry", "fragmentMain"}, {"parameterBlock", "Material"},
		    {"parameters", parameters},
		    {"renderState",
		        {{"cull", preview || m.doubleSided ? "none" : "back"}, {"depthTest", true},
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
