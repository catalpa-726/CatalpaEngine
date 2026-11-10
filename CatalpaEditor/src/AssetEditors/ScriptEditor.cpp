#include "Panels/EditorText.h"
#include "AssetEditors/ScriptEditor.h"
#include "Commands/GraphCommands.h"
#include "Scripting/ScriptAsset.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Scene/PrefabAsset.h"
#include "Materials/MaterialValidation.h"
#include "imgui.h"

namespace Catalpa::Editor {
    void ScriptEditor::Draw(Application&, AssetDocument& doc, const PanelText&, std::string& error) {
        constexpr size_t Limit = 4 * 1024 * 1024;
        if (m_Source != doc.Source()) {
            if (doc.Source()->size() > Limit) { error = "Author text exceeds editor budget"; return; }
            m_Buffer.assign(Limit + 1, 0);
            std::copy(doc.Source()->begin(), doc.Source()->end(), m_Buffer.begin());
            m_Source = doc.Source();
        }

        const bool changed = ImGui::InputTextMultiline(UiLabel("script.source", "Author source").c_str(), m_Buffer.data(), m_Buffer.size(), {-1, 380},
            ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::IsItemActivated()) {
            static std::atomic_uint64_t next{1};
            m_Group = next.fetch_add(1);
        }
        if (changed && ChangeAuthorSource(doc, std::string(m_Buffer.data()), error, m_Group)) m_Source = doc.Source();
        if (ImGui::IsItemDeactivated()) m_Group = 0;

        if (ImGui::Button(UiLabel("script.validate", "Validate source").c_str())) {
            bool valid = false;
            if (doc.TypeKey() == "prefab") {
                PrefabAsset value;
                valid = ReadPrefab(*doc.Source(), value, error);
            } else if (doc.TypeKey() == "material_definition") {
                std::shared_ptr<const MaterialDefinition> value;
                valid = ParseMaterialDefinition(*doc.Source(), value, error);
            } else {
                const auto first = doc.Source()->find_first_not_of(" \t\r\n");
                if (doc.AuthorPath().extension() == L".lua" || first == std::string::npos || (*doc.Source())[first] != '{')
                    valid = LuaScriptBackend::CheckSource(*doc.Source(), error);
                else {
                    ScriptAsset asset;
                    valid = ReadScriptAsset(*doc.Source(), asset, error) && LuaScriptBackend::CheckSource(asset.source, error);
                }
            }
            if (valid) error.clear();
        }

        if (doc.TypeKey() == "script")
            ImGui::TextWrapped(UiFormat("script.api_help", "Return a table with start(dt), update(dt), stop(dt). "
                "Use scene.translate(x,y,z), scene.set_translation(x,y,z), scene.position(), self and properties. "
                "require(name) resolves only declared cooked script modules."));
        else if (doc.TypeKey() == "prefab")
            ImGui::TextWrapped(UiFormat("prefab.source_help", "Prefab author JSON contains scene entities and stable nested slots. "
                "Build this file before assigning or updating instances."));
        else
            ImGui::TextWrapped(UiFormat("material.source_help", "Material definition author JSON uses the ordinary material schema. "
                "Validate and build it before creating dependent instances."));
    }
}
