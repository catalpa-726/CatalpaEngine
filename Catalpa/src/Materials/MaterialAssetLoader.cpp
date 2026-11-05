#include "catpch.h"
#include "Materials/MaterialAssetLoader.h"
#include "Materials/MaterialValidation.h"
#include "Assets/AssetManager.h"
#include "Shaders/ShaderAsset.h"
#include "Core/Serialization/JsonArchive.h"
namespace Catalpa {
    size_t MaterialDefinitionBytes(const MaterialDefinition& value) noexcept {
        size_t bytes = sizeof(value) + value.family.capacity() + value.vertexEntry.capacity() + value.fragmentEntry.capacity() + value.parameterBlock.capacity() + 4 + value.parameters.capacity() * sizeof(MaterialParameter);
        for (const auto& parameter : value.parameters) bytes += parameter.id.capacity() + parameter.displayName.capacity() + 2;
        return bytes;
    }
    size_t MaterialInstanceBytes(const MaterialInstance& value) noexcept {
        size_t bytes = sizeof(value) + value.parameters.capacity() * sizeof(MaterialParameterOverride);
        for (const auto& parameter : value.parameters) bytes += parameter.parameterId.capacity() + 1;
        return bytes;
    }
    namespace {
        bool ReflectanceTexture(const MaterialDefinition& definition, const MaterialParameter& parameter,
            const ImageAsset& image, std::string& error) {
            if (definition.shadingModel != MaterialShadingModel::Pbr ||
                parameter.textureSemantic != MaterialTextureSemantic::BaseColor ||
                image.image.format != PixelFormat::RGBA32_Float) return true;
            for (const auto& sub : image.image.subresources)
                for (uint32_t y = 0; y < sub.height; ++y)
                    for (uint32_t x = 0; x < sub.width; ++x) {
                        std::array<float, 4> texel;
                        std::memcpy(texel.data(), image.image.pixels.data() + sub.offset + uint64_t(y) * sub.rowPitch + uint64_t(x) * 16, 16);
                        if (texel[0] > 1 || texel[1] > 1 || texel[2] > 1) {
                            error = "PBR base-color texture is reflectance in [0,1]; HDR energy belongs to emissive";
                            return false;
                        }
                    }
            return true;
        }

        ArchiveLimits DecodeLimits(size_t bytes) {
            ArchiveLimits limits;
            // JSON trees contain nodes/keys in addition to text. Both byte and node caps follow
            // the admitted workspace; small budgets must not inherit the global 65k-node limit.
            limits.maxBytes = std::min(limits.maxBytes, bytes / 128);
            limits.maxStringBytes = std::min(limits.maxStringBytes, bytes / 128);
            limits.maxNodes = std::min(limits.maxNodes, bytes / 1024);
            limits.maxCollectionEntries = std::min(limits.maxCollectionEntries, limits.maxNodes);
            return limits;
        }
        bool TextPayload(const AssetContainer& container, const AssetDecodeContext& context, std::string_view& text, std::string& error) {
            // JSON value-tree scratch is bounded separately by ArchiveLimits; reserve ample decode space.
            if (container.payloadVersion != 1 || container.payload.size() > DecodeLimits(context.maxResidentBytes).maxBytes || context.cancellation.IsCancellationRequested()) {
                error = "Unsupported material payload version, cancellation or decode budget"; return false;
            }
            text = container.payload.empty() ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(container.payload.data()), container.payload.size());
            return true;
        }
    }
    bool RegisterMaterialAssetLoaders(AssetManager& manager, std::string& error) {
        AssetLoader definition, instance;
        if (!AssetType::TryParse("material_definition", definition.type) || !AssetType::TryParse("material_instance", instance.type)) { error = "Cannot create material type keys"; return false; }
        definition.cppType = std::type_index(typeid(MaterialDefinition)); instance.cppType = std::type_index(typeid(MaterialInstance));
        definition.decode = [](const AssetContainer& container, const AssetDecodeContext& context, AssetLoadResult& output, std::string& message) {
            std::string_view text; if (!TextPayload(container, context, text, message)) return false;
            std::shared_ptr<const MaterialDefinition> value; if (!ParseMaterialDefinition(text, value, message, DecodeLimits(context.maxResidentBytes))) return false;
            const auto shader = std::find_if(context.dependencies.begin(), context.dependencies.end(), [&](const auto& dependency) { return dependency.id == value->shader.id && dependency.cppType == std::type_index(typeid(ShaderAsset)); });
            if (shader == context.dependencies.end()) { message = "Material definition requires its loaded shader dependency"; return false; }
            // 解析只产生作者schema；在候选中固定Manager已经验证/保活的shader依赖，再发布const定义。
            MaterialDefinition resolved = *value;
            if (!AssetVersionRef<ShaderAsset>::TryCreate(shader->id, shader->version,
                std::static_pointer_cast<const ShaderAsset>(shader->value), resolved.shaderVersion, message)
                || !ValidateMaterialDefinition(resolved, message)) return false;
            value = std::make_shared<const MaterialDefinition>(std::move(resolved));
            for (const auto& parameter : value->parameters) if (const auto* texture = std::get_if<AssetHandle<ImageAsset>>(&parameter.defaultValue); texture && texture->HasValue()) {
                const auto dependency = std::find_if(context.dependencies.begin(), context.dependencies.end(), [&](const auto& item) { return item.id == texture->id && item.cppType == std::type_index(typeid(ImageAsset)); });
                if (dependency == context.dependencies.end()) { if (!parameter.allowTextureFallback) { message = "Material default texture dependency is missing"; return false; } }
                else if (!dependency->value || !ValidateMaterialTexture(parameter, *std::static_pointer_cast<const ImageAsset>(dependency->value), message)
                    || !ReflectanceTexture(*value, parameter, *std::static_pointer_cast<const ImageAsset>(dependency->value), message)) return false;
            }
            const auto bytes = MaterialDefinitionBytes(*value);
            if (bytes > context.maxResidentBytes) { message = "Material definition exceeds resident budget"; return false; }
            output = MakeAssetLoadResult<MaterialDefinition>(std::move(value), bytes); return true;
        };
        instance.decode = [](const AssetContainer& container, const AssetDecodeContext& context, AssetLoadResult& output, std::string& message) {
            std::string_view text; if (!TextPayload(container, context, text, message)) return false;
            std::unique_ptr<ArchiveValue> document; ArchiveError diagnostic;
            if (!JsonArchive::Parse(text, document, diagnostic, DecodeLimits(context.maxResidentBytes))) { message = diagnostic.ToString(); return false; }
            const auto* idField = document->Find("definitionId"); const auto* versionField = document->Find("definitionVersion");
            const auto* idText = idField ? std::get_if<std::string>(&idField->data) : nullptr; AssetId id; uint64_t version = 0;
            if (!idText || !AssetId::TryParse(*idText, id) || !versionField || !versionField->ReadUnsigned(version)) { message = "Material instance definition identity/version is invalid"; return false; }
            auto payload = context.FindDependency<MaterialDefinition>(id, version);
            AssetVersionRef<MaterialDefinition> reference;
            if (!payload || !AssetVersionRef<MaterialDefinition>::TryCreate(id, version, std::move(payload), reference, message)) { message = "Material instance definition version is not loaded"; return false; }
            document.reset();
            std::shared_ptr<const MaterialInstance> value; if (!ParseMaterialInstance(text, reference, value, message, DecodeLimits(context.maxResidentBytes))) return false;
            MaterialInstance resolved = *value;
            for (const auto& parameter : value->parameters) if (const auto* texture = std::get_if<AssetHandle<ImageAsset>>(&parameter.value); texture && texture->HasValue()) {
                const auto schema = std::find_if(value->definition.Get()->parameters.begin(), value->definition.Get()->parameters.end(), [&](const auto& item) { return item.id == parameter.parameterId; });
                const auto dependency = std::find_if(context.dependencies.begin(), context.dependencies.end(), [&](const auto& item) { return item.id == texture->id && item.cppType == std::type_index(typeid(ImageAsset)); });
                if (dependency == context.dependencies.end()) { if (schema == value->definition.Get()->parameters.end() || !schema->allowTextureFallback) { message = "Material instance texture dependency is missing"; return false; } }
                else if (schema == value->definition.Get()->parameters.end() || !dependency->value || !ValidateMaterialTexture(*schema, *std::static_pointer_cast<const ImageAsset>(dependency->value), message)
                    || !ReflectanceTexture(*value->definition.Get(), *schema, *std::static_pointer_cast<const ImageAsset>(dependency->value), message)) return false;
                if (dependency != context.dependencies.end()) {
                    constexpr std::array<std::string_view, 5> textureIds{"baseColor_texture", "normal_texture",
                        "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
                    const auto slot = std::find(textureIds.begin(), textureIds.end(), parameter.parameterId);
                    if (slot != textureIds.end() && !AssetVersionRef<ImageAsset>::TryCreate(dependency->id, dependency->version,
                        std::static_pointer_cast<const ImageAsset>(dependency->value),
                        resolved.textureVersions[size_t(slot - textureIds.begin())], message)) return false;
                }
            }
            value = std::make_shared<const MaterialInstance>(std::move(resolved));
            const auto bytes = MaterialInstanceBytes(*value);
            if (bytes > context.maxResidentBytes) { message = "Material instance exceeds resident budget"; return false; }
            output = MakeAssetLoadResult<MaterialInstance>(std::move(value), bytes); return true;
        };
        return manager.RegisterLoader(std::move(definition), error) && manager.RegisterLoader(std::move(instance), error);
    }
}
