#include "Panels/ProjectSettingsPanel.h"
#include "Application/EditorProject.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "imgui.h"

namespace Catalpa::Editor {
    void ProjectSettingsPanel::Draw(EditorProject& project, bool busy, std::string& error) {
        const auto* session = project.GetSession();
        if (!session) return;
        if (m_Project != session->config.id) {
            m_Project = session->config.id;
            m_Draft = session->config;
            const std::array values{m_Draft->name, Tools::AssetPathText(m_Draft->contentRoot),
                Tools::AssetPathText(m_Draft->derivedDataRoot), Tools::AssetPathText(m_Draft->cookedRoot),
                Tools::AssetPathText(m_Draft->buildProfilesFile), m_Draft->buildProfile};
            m_FieldOverflow = false;
            for (size_t i = 0; i < values.size(); ++i) {
                m_Fields[i].fill(0);
                if (values[i].size() >= m_Fields[i].size()) {
                    m_FieldOverflow = true;
                    continue;
                }
                std::copy_n(values[i].data(), values[i].size(), m_Fields[i].data());
            }
        }
        if (!ImGui::CollapsingHeader("Author project settings")) return;
        // 配置可以包含长路径；编辑器容量不足时拒绝保存，不把截断内容写回项目。
        if (m_FieldOverflow) {
            ImGui::TextWrapped("A project field exceeds the editor's 2047-byte limit. "
                "This panel cannot edit it; the project file is unchanged.");
            return;
        }
        ImGui::BeginDisabled(busy);
        constexpr const char* labels[]{"Project name", "Content root", "Derived data root", "Cooked root", "Build profiles", "Build profile"};
        for (size_t i = 0; i < m_Fields.size(); ++i) ImGui::InputText(labels[i], m_Fields[i].data(), m_Fields[i].size());
        if (ImGui::Button("Validate and save project")) {
            auto candidate = *m_Draft;
            candidate.name = m_Fields[0].data();
            candidate.contentRoot = std::filesystem::u8path(m_Fields[1].data());
            candidate.derivedDataRoot = std::filesystem::u8path(m_Fields[2].data());
            candidate.cookedRoot = std::filesystem::u8path(m_Fields[3].data());
            candidate.buildProfilesFile = std::filesystem::u8path(m_Fields[4].data());
            candidate.buildProfile = m_Fields[5].data();
            if (project.ApplyConfig(candidate, error)) Reset();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("External source roots remain explicitly declared in the project file.");
    }
}
