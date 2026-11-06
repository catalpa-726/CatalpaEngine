#include "Documents/AssetDocument.h"
#include "Core/Serialization/AuthorJson.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa::Editor {
    AssetDocument::AssetDocument(StableId id, AssetId asset, std::string type, std::string source)
        : Document(id), m_Asset(asset), m_Version(1), m_Type(std::move(type)),
          m_Source(std::make_shared<const std::string>(std::move(source))) {}

    bool AssetDocument::ReplaceSource(const std::shared_ptr<const std::string>& expected,
                                      std::shared_ptr<const std::string> next, std::string& error) {
        if (!next || !m_Source || m_Source != expected || next->size() > 16 * 1024 * 1024) {
            error = "Source draft changed or exceeds the author limit";
            return false;
        }
        m_Source = std::move(next);
        return true;
    }

    bool AssetDocument::Serialize(std::string& output, std::string& error) const try {
        if (m_Source) { output = *m_Source; return true; }
        if (!m_Material || !ValidateMaterialInstance(*m_Material, error)) {
            if (error.empty()) error = "Image sources are read-only";
            return false;
        }
        using AuthorJson::Json;
        const auto& material = *m_Material;
        Json parameters = Json::object();
        for (const auto& p : material.parameters) {
            const auto schema = std::find_if(material.definition.Get()->parameters.begin(), material.definition.Get()->parameters.end(),
                [&](const auto& field) { return field.id == p.parameterId; });
            if (schema == material.definition.Get()->parameters.end()) { error = "Material author parameter is undeclared"; return false; }
            // 磁盘只记录覆盖值；只读 sampler 默认值不能被误编码成禁止的用户覆盖。
            if (schema->defaultValue == p.value) continue;
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, AssetHandle<ImageAsset>>) parameters[p.parameterId] = value.id.ToString();
                else parameters[p.parameterId] = value;
            }, p.value);
        }
        Json states = Json::object();
        const auto& base = material.definition.Get()->renderState;
        const auto& current = material.renderState;
        if (base.cull != current.cull) {
            const char* names[]{"none", "back", "front"};
            states["cull"] = names[uint32_t(current.cull)];
        }
        if (base.blend != current.blend) {
            const char* names[]{"opaque", "alpha", "additive"};
            states["blend"] = names[uint32_t(current.blend)];
        }
        if (base.depthTest != current.depthTest) states["depthTest"] = current.depthTest;
        if (base.depthWrite != current.depthWrite) states["depthWrite"] = current.depthWrite;
        const Json document{{"schemaVersion", 1}, {"kind", "instance"},
            {"definitionId", material.definition.GetId().ToString()}, {"definitionVersion", material.definition.GetVersion()},
            {"revision", material.revision}, {"parameters", parameters}, {"renderState", states}};
        std::shared_ptr<const MaterialInstance> checked;
        const auto text = document.dump(2);
        if (!ParseMaterialInstance(text, material.definition, checked, error)) return false;
        output = text;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
    AssetDocument::AssetDocument(StableId id, AssetVersionRef<MaterialInstance> source)
        : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()),
          m_Material(source.GetShared()) {}

    AssetDocument::AssetDocument(StableId id, AssetVersionRef<ImageAsset> source)
        : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()), m_Image(std::move(source)) {}

    bool AssetDocument::ReplaceRecipe(const std::shared_ptr<const Tools::AssetBuildRequest>& expected,
                                      std::shared_ptr<const Tools::AssetBuildRequest> next,
                                      std::string& error) {
        if (!next || !m_Recipe || m_Recipe != expected) {
            error = "Import settings changed outside this transaction";
            return false;
        }
        m_Recipe = std::move(next);
        error.clear();
        return true;
    }

    bool AssetDocument::ReplaceMaterial(const std::shared_ptr<const MaterialInstance>& expected,
                                        std::shared_ptr<const MaterialInstance> next, std::string& error) {
        if (!next || !m_Material || m_Material != expected) {
            error = "Material draft has changed";
            return false;
        }
        m_Material = std::move(next);
        error.clear();
        return true;
    }
}
