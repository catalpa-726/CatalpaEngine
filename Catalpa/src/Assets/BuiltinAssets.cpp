#include "catpch.h"
#include "Assets/BuiltinAssets.h"
#include "Assets/BuiltinAssetIds.h"
#include "Assets/AssetManager.h"
#include "Images/ImageAsset.h"
#include "Images/ImageValidation.h"
#include "Materials/MaterialAssetLoader.h"
#include "Materials/MaterialValidation.h"
#include "Shaders/ShaderLibrary.h"
namespace Catalpa {
    bool RegisterBuiltinAssets(AssetManager& manager, std::string& error) {
        try {
            // 必需shader必须先Ready；各着色模型固定自己的版本，不能让PBR定义引用Unlit字节码。
            AssetId shader, pbrShader;
            AssetVersionRef<ShaderAsset> shaderVersion, pbrShaderVersion;
            bool shaderFallback = false;
            if (!ShaderLibrary::BuiltinId("Unlit.slang", shader, error)
                || !manager.Acquire(shader, shaderVersion, shaderFallback, error) || shaderFallback
                || !ValidateShaderAsset(*shaderVersion.Get(), error)) {
                if (error.empty()) error = "Builtin materials require the ready Unlit shader version";
                return false;
            }
            if (!ShaderLibrary::BuiltinId("Pbr.slang", pbrShader, error) ||
                !manager.Acquire(pbrShader, pbrShaderVersion, shaderFallback, error) || shaderFallback ||
                !ValidateShaderAsset(*pbrShaderVersion.Get(), error)) {
                if (error.empty()) error = "Builtin PBR materials require the ready Pbr shader version";
                return false;
            }
            struct Pixel { AssetId id; std::array<uint8_t, 4> rgba; ImageSemantic semantic; };
            const std::array pixels{
                Pixel{BuiltinAssetIds::WhiteImage, {255,255,255,255}, ImageSemantic::Color},
                Pixel{BuiltinAssetIds::FlatNormalImage, {128,128,255,255}, ImageSemantic::Normal},
                Pixel{BuiltinAssetIds::ErrorImage, {255,0,255,255}, ImageSemantic::Color}};
            for (const auto& pixel : pixels) {
                ImageAsset image; image.sourceUri = "builtin:mathematical-rgba8";
                image.image.format = PixelFormat::RGBA8_UNorm; image.image.width = image.image.height = 1; image.image.semantic = pixel.semantic;
                image.image.subresources.push_back({0,0,0,1,1,1,0,4,4,4});
                for (auto byte : pixel.rgba) image.image.pixels.push_back(std::byte(byte));
                if (!ValidateImageAsset(image, error)) return false;
                const size_t bytes = sizeof(ImageAsset) + image.sourceUri.capacity() + 1 + image.image.subresources.capacity() * sizeof(ImageSubresource) + image.image.pixels.capacity();
                if (!manager.PublishBuiltin<ImageAsset>(pixel.id, 1, std::make_shared<const ImageAsset>(std::move(image)), bytes, error)) return false;
            }
            struct Material { BuiltinMaterialKind kind; AssetId definition, instance; };
            const std::array materials{
                Material{BuiltinMaterialKind::Unlit, BuiltinAssetIds::DefaultUnlitDefinition, BuiltinAssetIds::DefaultUnlitInstance},
                Material{BuiltinMaterialKind::Pbr, BuiltinAssetIds::DefaultPbrDefinition, BuiltinAssetIds::DefaultPbrInstance},
                Material{BuiltinMaterialKind::Error, BuiltinAssetIds::ErrorDefinition, BuiltinAssetIds::ErrorInstance}};
            for (const auto& item : materials) {
                std::shared_ptr<const MaterialDefinition> definition;
                const bool pbr = item.kind == BuiltinMaterialKind::Pbr;
                if (!CreateBuiltinMaterialDefinition(item.kind, pbr ? pbrShader : shader, definition, error)) return false;
                MaterialDefinition resolved = *definition;
                resolved.shaderVersion = pbr ? pbrShaderVersion : shaderVersion;
                if (!ValidateMaterialDefinition(resolved, error)) return false;
                definition = std::make_shared<const MaterialDefinition>(std::move(resolved));
                const size_t definitionBytes = MaterialDefinitionBytes(*definition);
                if (!manager.PublishBuiltin<MaterialDefinition>(item.definition, 1, std::move(definition), definitionBytes, error)) return false;
                AssetVersionRef<MaterialDefinition> fixed; bool fallback = false;
                if (!manager.Acquire(item.definition, fixed, fallback, error) || fallback) return false;
                std::shared_ptr<const MaterialInstance> instance;
                if (!CreateMaterialInstance(fixed, {}, {}, 1, instance, error)) return false;
                const size_t instanceBytes = MaterialInstanceBytes(*instance);
                if (!manager.PublishBuiltin<MaterialInstance>(item.instance, 1, std::move(instance), instanceBytes, error)) return false;
            }
            return manager.SetFallback<ImageAsset>(BuiltinAssetIds::ErrorImage, error)
                && manager.SetFallback<MaterialDefinition>(BuiltinAssetIds::ErrorDefinition, error)
                && manager.SetFallback<MaterialInstance>(BuiltinAssetIds::ErrorInstance, error);
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
