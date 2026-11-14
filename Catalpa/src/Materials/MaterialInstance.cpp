#include "catpch.h"

#include "Materials/MaterialInstance.h"

#include "Materials/MaterialValidation.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 在候选材质实例上应用参数与渲染状态覆盖。
		 * 在候选实例应用按稳定ID查找的受限覆盖，任何失败都不触及发布中的旧实例。
		 *
		 * @param[in,out] candidate 接收覆盖的候选材质实例，尚未对外发布。
		 * @param[in] overrides 按稳定参数标识指定的材质覆盖值。
		 * @param[in] states 需要覆盖的渲染状态，未指定项沿用原设置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 所有覆盖合法且结果实例通过校验时为 true，否则为 false。
		 */
		bool ApplyOverrides(MaterialInstance& candidate, std::span<const MaterialParameterOverride> overrides,
		    const MaterialStateOverrides& states, std::string& error) {
			const auto& definition = *candidate.definition.Get();
			std::set<std::string_view> seen;
			for (const auto& change : overrides) {
				if (!seen.insert(change.parameterId).second) {
					error = "Duplicate material override: " + change.parameterId;
					return false;
				}
				const auto parameter = std::find_if(definition.parameters.begin(), definition.parameters.end(),
				    [&](const auto& value) { return value.id == change.parameterId; });
				if (parameter == definition.parameters.end()) {
					error = "Unknown material parameter: " + change.parameterId;
					return false;
				}
				if (!parameter->allowOverride) {
					error = "Material parameter does not allow overrides: " + change.parameterId;
					return false;
				}
				if (!ValidateMaterialValue(*parameter, change.value, error))
					return false;
				const auto destination = std::find_if(candidate.parameters.begin(), candidate.parameters.end(),
				    [&](const auto& value) { return value.parameterId == change.parameterId; });
				if (destination == candidate.parameters.end()) {
					error = "Material candidate is missing a declared parameter";
					return false;
				}
				destination->value = change.value;
				constexpr std::array<std::string_view, 5> textureIds{"baseColor_texture", "normal_texture",
				    "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
				const auto slot = std::find(textureIds.begin(), textureIds.end(), change.parameterId);
				if (slot != textureIds.end()) {
					const auto* handle = std::get_if<AssetHandle<ImageAsset>>(&change.value);
					auto& fixed = candidate.textureVersions[size_t(slot - textureIds.begin())];
					if (!handle || !handle->HasValue() || fixed.GetId() != handle->id)
						fixed = {}; /* 新纹理须由资产解析步骤固定，不能借用旧内容或隐式取latest。 */
				}
			}
			return ResolveMaterialRenderState(
			           definition, candidate.renderState, states, candidate.renderState, error) &&
			    ValidateMaterialInstance(candidate, error);
		}
	}

	bool CreateMaterialInstance(const AssetVersionRef<MaterialDefinition>& definition,
	    std::span<const MaterialParameterOverride> overrides, const MaterialStateOverrides& states, uint64_t revision,
	    std::shared_ptr<const MaterialInstance>& output, std::string& error) {
		if (!definition.HasValue() || revision == 0) {
			error = "Material requires a fixed definition version and nonzero revision";
			return false;
		}
		if (!ValidateMaterialDefinition(*definition.Get(), error))
			return false;
		try {
			MaterialInstance candidate;
			candidate.definition = definition;
			candidate.revision = revision;
			candidate.renderState = definition.Get()->renderState;
			candidate.parameters.reserve(definition.Get()->parameters.size());
			for (const auto& parameter : definition.Get()->parameters)
				candidate.parameters.push_back({parameter.id, parameter.defaultValue});
			if (!ApplyOverrides(candidate, overrides, states, error))
				return false;
			auto published = std::make_shared<const MaterialInstance>(std::move(candidate));
			output.swap(published);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool UpdateMaterialInstance(const MaterialInstance& source, std::span<const MaterialParameterOverride> overrides,
	    const MaterialStateOverrides& states, std::shared_ptr<const MaterialInstance>& output, std::string& error) {
		if (!ValidateMaterialInstance(source, error))
			return false;
		if (source.revision == std::numeric_limits<uint64_t>::max()) {
			error = "Material instance revision exhausted";
			return false;
		}
		try {
			MaterialInstance candidate = source;
			++candidate.revision;
			if (!ApplyOverrides(candidate, overrides, states, error))
				return false;
			auto published = std::make_shared<const MaterialInstance>(std::move(candidate));
			output.swap(published);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	const MaterialValue* FindMaterialParameter(const MaterialInstance& instance, std::string_view id) noexcept {
		const auto found = std::find_if(instance.parameters.begin(), instance.parameters.end(),
		    [&](const auto& value) { return value.parameterId == id; });
		return found == instance.parameters.end() ? nullptr : &found->value;
	}
}
