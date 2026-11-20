#include "Panels/EditorText.h"
#include "AssetEditors/ScriptEditor.h"

#include "Commands/GraphCommands.h"
#include "Scene/PrefabAsset.h"
#include "Materials/MaterialValidation.h"
#include "imgui.h"

namespace Catalpa::Editor {
	void ScriptEditor::Draw(Application&, AssetDocument& doc, const PanelText&, std::string& error) {
		if (doc.TypeKey() == "script") {
			ImGui::TextWrapped("%s", UiText("script.recovered_source",
			    "Recovered script. Use Documents > Save As to keep this copy, then open it in your script editor."));
			return;
		}
		constexpr size_t Limit = 4 * 1024 * 1024;
		if (m_Source != doc.Source()) {
			if (doc.Source()->size() > Limit) {
				error = "Author text exceeds editor budget";
				return;
			}
			m_Buffer.assign(Limit + 1, 0);
			std::copy(doc.Source()->begin(), doc.Source()->end(), m_Buffer.begin());
			m_Source = doc.Source();
		}

		const bool changed = ImGui::InputTextMultiline(UiLabel("script.source", "Author source").c_str(),
		    m_Buffer.data(), m_Buffer.size(), {-1, 380}, ImGuiInputTextFlags_AllowTabInput);
		if (ImGui::IsItemActivated()) {
			static std::atomic_uint64_t next{1};
			m_Group = next.fetch_add(1);
		}
		if (changed && ChangeAuthorSource(doc, std::string(m_Buffer.data()), error, m_Group))
			m_Source = doc.Source();
		if (ImGui::IsItemDeactivated())
			m_Group = 0;

		if (ImGui::Button(UiLabel("script.validate", "Validate source").c_str())) {
			bool valid = false;
			if (doc.TypeKey() == "prefab") {
				PrefabAsset value;
				valid = ReadPrefab(*doc.Source(), value, error);
			} else if (doc.TypeKey() == "material_definition") {
				std::shared_ptr<const MaterialDefinition> value;
				valid = ParseMaterialDefinition(*doc.Source(), value, error);
			}
			if (valid)
				error.clear();
		}

		if (doc.TypeKey() == "prefab")
			ImGui::TextWrapped(UiFormat("prefab.source_help",
			    "Prefab author JSON contains scene entities and stable nested slots. "
			    "Build this file before assigning or updating instances."));
		else
			ImGui::TextWrapped(UiFormat("material.source_help",
			    "Material definition author JSON uses the ordinary material schema. "
			    "Validate and build it before creating dependent instances."));
	}
}
