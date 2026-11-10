#include "catpch.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/BuildPanel.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Core/PlatformServices.h"
#include "imgui.h"
#include "Panels/EditorText.h"

namespace Catalpa::Editor {
    void ContentBrowserPanel::ClearSelection() {
        m_Selected.clear();
        m_Remaps.clear();
        m_RemapUri = {};
        m_RemapTarget = {};
    }

    void ContentBrowserPanel::Reset() {
        ClearSelection();
        m_Directory.clear();
        m_Browser.Reset();
        m_Error.clear();
        m_Root = 0;
        m_UnitScale = 1;
        m_Up = 0;
        m_Normals = true;
        m_Tangents = true;
    }

    void ContentBrowserPanel::Draw(bool& visible, const Tools::ProjectSession* session,
                                   const std::filesystem::path& shaderRoot, BuildPanel& build,
                                   const PanelText& text) {
        if (!visible)
            return;
        if (ImGui::Begin(text("panel.content", "Content Browser", "Catalpa.Content").c_str(), &visible)) {
            if (!session)
                ImGui::TextWrapped(
                    "%s", text("content.open_project",
                               "Open an author project from File to select read-only source files.", nullptr)
                              .c_str());
            else {
                std::vector<std::pair<std::string, std::filesystem::path>> roots{
                    {UiText("browser.content","Content"), session->config.contentRoot}};
                for (const auto& [name, path] : session->config.externalSourceRoots)
                    roots.push_back({name, path});
                if (m_Root >= roots.size())
                    m_Root = 0;
                if (m_Directory.empty()) {
                    m_Directory = roots[m_Root].second;
                }
                if (ImGui::BeginCombo(text("content.root", "Read-only root", "ContentRoot").c_str(),
                                      roots[m_Root].first.c_str())) {
                    for (size_t i = 0; i < roots.size(); ++i) {
                        ImGui::PushID(int(i));
                        if (ImGui::Selectable(roots[i].first.c_str(), m_Root == i)) {
                            m_Root = i;
                            m_Directory = roots[i].second;
                            ClearSelection();
                            m_Browser.Reset();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (m_Browser.Draw(roots[m_Root].second, roots[m_Root].first, m_Directory, m_Selected,
                                   ImGui::GetTextLineHeightWithSpacing() * 7, text)) {
                    m_Remaps.clear();
                    m_RemapUri = {};
                    m_RemapTarget = {};
                    m_Error.clear();
                }
                ImGui::BeginDisabled(build.Busy());
                ImGui::InputDouble(
                    text("content.unit_scale", "Additional unit scale", "ImportUnitScale").c_str(),
                    &m_UnitScale, 0, 0, "%.6g");
                const char* axes[]{UiText("content.up_auto","Auto / source metadata"), "Y", "Z"};
                ImGui::Combo(text("content.up_axis", "Source up axis", "ImportUpAxis").c_str(), &m_Up,
                             axes, 3);
                ImGui::Checkbox(
                    text("content.normals", "Generate missing face normals", "GenerateNormals").c_str(),
                    &m_Normals);
                ImGui::Checkbox(
                    text("content.tangents", "Generate missing MikkTSpace tangents", "GenerateTangents")
                        .c_str(),
                    &m_Tangents);
                if (ImGui::TreeNode(
                        text("content.remap", "Explicit dependency remaps", "DependencyRemaps").c_str())) {
                    ImGui::InputText(text("content.remap_uri", "Original URI", "RemapURI").c_str(),
                                     m_RemapUri.data(), m_RemapUri.size());
                    ImGui::InputText(text("content.remap_target", "Replacement path", "RemapTarget").c_str(),
                                     m_RemapTarget.data(), m_RemapTarget.size());
                    if (ImGui::Button(
                            text("content.remap_add", "Add / replace mapping", "AddRemap").c_str()) &&
                        m_RemapUri[0] && m_RemapTarget[0]) {
                        if (m_Remaps.contains(m_RemapUri.data()) || m_Remaps.size() < 64)
                            m_Remaps.insert_or_assign(m_RemapUri.data(), m_RemapTarget.data());
                        else
                            m_Error = "At most 64 dependency remaps can be added";
                    }
                    if (ImGui::Button(text("content.remap_clear", "Clear mappings", "ClearRemaps").c_str()))
                        m_Remaps.clear();
                    for (const auto& [uri, target] : m_Remaps)
                        ImGui::TextWrapped("%s -> %s", uri.c_str(), target.c_str());
                    ImGui::TreePop();
                }
                // 默认导入器只按扩展查询，整个浏览会话复用注册表。
                static const auto importers = Tools::AssetImportRegistry::Defaults();
                std::error_code fileError;
                const bool supported = !m_Selected.empty() && importers.Find(m_Selected) &&
                                       std::filesystem::is_regular_file(m_Selected, fileError);
                ImGui::BeginDisabled(!supported);
                if (ImGui::Button(
                        text("content.build", "Build and load selected source", "BuildSource").c_str())) {
                    Tools::AssetBuildRequest request;
                    request.project = session->config;
                    request.source = m_Selected;
                    request.shaderRoot = shaderRoot;
                    request.settings.unitScale = m_UnitScale;
                    request.settings.sourceUp = m_Up == 1 ? "y" : m_Up == 2 ? "z" : "auto";
                    request.settings.generateNormals = m_Normals;
                    request.settings.generateTangents = m_Tangents;
                    request.settings.dependencyRemaps = m_Remaps;
                    if (build.Start(std::move(request), m_Error))
                        m_Error.clear();
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::TextWrapped("%s", text("content.scope",
                                              "glTF / GLB / FBX / OBJ, PNG / JPEG / TGA / BMP and HDR environments. Sources "
                                              "stay read-only; Cooked and DerivedDataCache are separate.",
                                              nullptr)
                                             .c_str());
                if (!m_Selected.empty())
                    ImGui::TextWrapped("%s", Tools::AssetPathText(m_Selected).c_str());
                if (!m_Error.empty())
                    ImGui::TextWrapped("%s", m_Error.c_str());
            }
        }
        ImGui::End();
    }
}
