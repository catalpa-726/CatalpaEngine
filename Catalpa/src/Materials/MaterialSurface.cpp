#include "catpch.h"
#include "Materials/MaterialSurface.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa {

    bool ResolveMaterialSurface(const MaterialInstance& instance, MaterialSurfaceParameters& output, std::string& error) {
        if (!ValidateMaterialInstance(instance, error))
            return false;
        MaterialSurfaceParameters candidate;
        for (const auto& parameter : instance.parameters) {
            const auto& id = parameter.parameterId;
            const auto& value = parameter.value;
            bool matched = true;
            const auto scalar = [&](float& target) {
                const auto* number = std::get_if<float>(&value);
                if (!number) return false;
                target = *number;
                return true;
            };
            if (id == "base_color") {
                if (const auto* v = std::get_if<std::array<float, 4>>(&value)) candidate.baseColor = *v;
                else matched = false;
            } else if (id == "emissive") {
                if (const auto* v = std::get_if<std::array<float, 3>>(&value)) candidate.emissive = *v;
                else matched = false;
            } else if (id == "metallic") matched = scalar(candidate.metallic);
            else if (id == "roughness") matched = scalar(candidate.roughness);
            else if (id == "normal_scale") matched = scalar(candidate.normalScale);
            else if (id == "occlusion_strength") matched = scalar(candidate.occlusionStrength);
            else if (id == "alpha_cutoff") matched = scalar(candidate.alphaCutoff);
            else if (id == "uv_flip_v") {
                if (const auto* v = std::get_if<bool>(&value)) candidate.flipV = *v;
                else matched = false;
            } else if (id == "uv_scale_offset") {
                if (const auto* v = std::get_if<std::array<float, 4>>(&value)) candidate.uvScaleOffset = *v;
                else matched = false;
            } else if (id == "uv_cycles") {
                if (const auto* v = std::get_if<std::array<float, 2>>(&value)) candidate.uvCycles = *v;
                else matched = false;
            } else if (id == "pulse_cycles") {
                if (const auto* v = std::get_if<uint32_t>(&value)) candidate.pulseCycles = *v;
                else matched = false;
            } else if (id.ends_with("_sampler")) {
                constexpr std::array<std::string_view, 5> ids{"baseColor_sampler", "normal_sampler",
                    "metallicRoughness_sampler", "emissive_sampler", "occlusion_sampler"};
                const auto found = std::find(ids.begin(), ids.end(), id);
                const auto* flags = std::get_if<uint32_t>(&value);
                matched = found != ids.end() && flags;
                if (matched) candidate.samplerFlags[size_t(found - ids.begin())] = *flags;
            } else {
                constexpr std::array<std::string_view, 5> ids{"baseColor_texture", "normal_texture",
                    "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
                const auto found = std::find(ids.begin(), ids.end(), id);
                const auto* texture = std::get_if<AssetHandle<ImageAsset>>(&value);
                matched = found != ids.end() && texture;
                if (matched) candidate.textures[size_t(found - ids.begin())] = *texture;
            }
            if (!matched) {
                error = "Unsupported surface parameter or incompatible type: " + id;
                return false;
            }
        }
        const auto nonnegative = [](float v) { return std::isfinite(v) && v >= 0; };
        if (!std::all_of(candidate.baseColor.begin(), candidate.baseColor.end(), nonnegative) || candidate.baseColor[3] > 1 ||
            !std::all_of(candidate.emissive.begin(), candidate.emissive.end(), nonnegative) ||
            candidate.metallic < 0 || candidate.metallic > 1 || candidate.roughness < 0 || candidate.roughness > 1 ||
            candidate.occlusionStrength < 0 || candidate.occlusionStrength > 1 || candidate.alphaCutoff < 0 ||
            candidate.pulseCycles > 65536) {
            error = "Surface color, alpha, metallic/roughness or occlusion is outside its physical domain";
            return false;
        }
        for (const float value : candidate.baseColor) {
            if (value > 1e8f) { error = "Surface output exceeds the explicit 1e8 linear-value budget"; return false; }
        }
        for (const float value : candidate.emissive) {
            if (value > 1e8f) { error = "Emissive exceeds the explicit 1e8 linear-value budget"; return false; }
        }
        if (std::abs(candidate.normalScale) > 1e8f) { error = "Normal scale exceeds the finite shading budget"; return false; }
        for (const float value : candidate.uvScaleOffset) {
            if (std::abs(value) > 1e8f) { error = "UV transform exceeds the explicit 1e8 finite-coordinate budget"; return false; }
        }
        if (candidate.textures[1].HasValue() && (candidate.uvScaleOffset[0] == 0 || candidate.uvScaleOffset[1] == 0)) {
            error = "A tangent-space normal map requires a nonsingular UV scale";
            return false;
        }
        for (float cycles : candidate.uvCycles) {
            if (!std::isfinite(cycles) || std::abs(cycles) > 65536 || std::trunc(cycles) != cycles) {
                error = "UV time cycles must be integers in [-65536,65536] per 4096 simulation seconds";
                return false;
            }
        }
        output = std::move(candidate);
        error.clear();
        return true;
    }

}
