#include "Panels/InspectorPanel.h"
#include "Commands/SceneCommands.h"

#include "imgui.h"

namespace Catalpa::Editor {
    void InspectorPanel::DrawName(SceneDocument& doc, EntityReference ref, const IdentityComponent& identity,
                                  const PanelText& text, std::string& error) {
        constexpr size_t MaxNameBytes = 65536; // 与 ComponentFieldKind::String 的作者字节上限一致。
        if (identity.name.size() > MaxNameBytes) {
            ImGui::TextUnformatted(identity.name.c_str());
            return;
        }
        if (m_NameBuffer.empty())
            m_NameBuffer.resize(MaxNameBytes + 1);
        std::copy(identity.name.begin(), identity.name.end(), m_NameBuffer.begin());
        m_NameBuffer[identity.name.size()] = '\0';
        const bool submit =
            ImGui::InputText(text("edit.entity_name", "Name", "EntityName").c_str(), m_NameBuffer.data(),
                             m_NameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemActivated() || m_NameTarget != ref) {
            m_NameAtActivation = identity.name;
            m_NameTarget = ref;
        }
        if (submit)
            RenameEntity(doc, ref.persistent, m_NameBuffer.data(), m_NameAtActivation, error);
        if (ImGui::IsItemDeactivated())
            m_NameTarget = {};
    }

    bool InspectorPanel::Draw(SceneDocument& doc, const EditorSelection& selection, const PanelText& text,
                              std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene)
            return false;
        const auto ref = scene->MakeReference(selection.Resolve(doc));
        if (!ref.HasValue())
            return false;
        IdentityComponent id;
        if (!scene->GetIdentity(ref.resolved, id).IsSuccess())
            return false;
        ImGui::PushID(id.persistent.ToString().c_str());
        DrawName(doc, ref, id, text, error);
        ImGui::TextDisabled("%s", id.persistent.ToString().c_str());
        m_Properties.Draw(doc, ref, text, error);
        ImGui::PopID();
        return true;
    }
}
