#include "Commands/AssetCommands.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa::Editor {
    namespace {
        using Recipe = std::shared_ptr<const Tools::AssetBuildRequest>;

        class ImportEdit final : public EditorCommand {
          public:
            ImportEdit(StableId id, Recipe before, Recipe after)
                : EditorCommand(id), m_Before(std::move(before)), m_After(std::move(after)) {}

            bool Apply(Document& doc, std::string& error) override {
                return static_cast<AssetDocument&>(doc).ReplaceRecipe(m_Before, m_After, error);
            }

            bool Revert(Document& doc, std::string& error) override {
                return static_cast<AssetDocument&>(doc).ReplaceRecipe(m_After, m_Before, error);
            }

          private:
            Recipe m_Before, m_After;
        };

        class MaterialEdit final : public EditorCommand {
          public:
            MaterialEdit(StableId doc, std::shared_ptr<const MaterialInstance> before,
                         std::shared_ptr<const MaterialInstance> after)
                : EditorCommand(doc), m_Before(std::move(before)), m_After(std::move(after)) {}

            bool Apply(Document& doc, std::string& error) override {
                return static_cast<AssetDocument&>(doc).ReplaceMaterial(m_Before, m_After, error);
            }

            bool Revert(Document& doc, std::string& error) override {
                return static_cast<AssetDocument&>(doc).ReplaceMaterial(m_After, m_Before, error);
            }

          private:
            std::shared_ptr<const MaterialInstance> m_Before, m_After;
        };
    }

    bool ChangeImportSettings(AssetDocument& doc, double unitScale, bool normals, bool tangents,
                              std::string& error) {
        if (!doc.Recipe() || !std::isfinite(unitScale) || unitScale <= 0 || unitScale > 1.e9) {
            error = "Import scale must be finite and in (0, 1e9]";
            return false;
        }
        auto next = std::make_shared<Tools::AssetBuildRequest>(*doc.Recipe());
        next->settings.unitScale = unitScale;
        next->settings.generateNormals = normals;
        next->settings.generateTangents = tangents;
        return doc.History().Execute(
            doc, std::make_unique<ImportEdit>(doc.Id(), doc.Recipe(), std::move(next)), error);
    }

    bool ChangeMaterial(AssetDocument& doc, std::span<const MaterialParameterOverride> parameters,
                        const MaterialStateOverrides& states, std::string& error,
                        const AssetManager* assets) {
        if (!doc.Material()) {
            error = "Document has no material";
            return false;
        }
        std::shared_ptr<const MaterialInstance> next;
        if (!UpdateMaterialInstance(*doc.Material(), parameters, states, next, error))
            return false;
        // 显式改绑已有贴图时固定当前加载版本；不接受不匹配语义、回退图或隐式跨版本替换。
        constexpr std::array<std::string_view, 5> slots{"baseColor_texture", "normal_texture",
                                                        "metallicRoughness_texture", "emissive_texture",
                                                        "occlusion_texture"};
        std::shared_ptr<MaterialInstance> resolved;
        for (const auto& change : parameters) {
            const auto* handle = std::get_if<AssetHandle<ImageAsset>>(&change.value);
            if (!handle || !handle->HasValue())
                continue;
            const auto it = std::find(slots.begin(), slots.end(), change.parameterId);
            if (it == slots.end()) {
                error = "Texture role has no runtime preview binding";
                return false;
            }
            const auto slot = static_cast<size_t>(it - slots.begin());
            if (next->textureVersions[slot].GetId() == handle->id)
                continue;
            const auto& schema = next->definition.Get()->parameters;
            const auto field = std::find_if(schema.begin(), schema.end(),
                                            [&](const auto& p) { return p.id == change.parameterId; });
            AssetVersionRef<ImageAsset> fixed;
            bool fallback = false;
            if (!assets || field == schema.end() || !assets->Acquire(handle->id, fixed, fallback, error) ||
                fallback) {
                error = "Load the selected texture before assigning it to this draft";
                return false;
            }
            if (!ValidateMaterialTexture(*field, *fixed.Get(), error))
                return false;
            if (!resolved)
                resolved = std::make_shared<MaterialInstance>(*next);
            resolved->textureVersions[slot] = std::move(fixed);
        }
        if (resolved)
            next = std::move(resolved);
        return doc.History().Execute(
            doc, std::make_unique<MaterialEdit>(doc.Id(), doc.Material(), std::move(next)), error);
    }
}
