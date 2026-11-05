#include "catpch.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/BuildPanel.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Core/PlatformServices.h"
#include "imgui.h"

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
        m_Files.clear();
        m_Error.clear();
        m_Root = 0;
        m_Filter = {};
        m_UnitScale = 1;
        m_Up = 0;
        m_Normals = true;
        m_Tangents = true;
    }

    void ContentBrowserPanel::Refresh(const std::filesystem::path& root) {
        m_Files.clear();
        m_Error.clear();
        try {
            m_Directory = std::filesystem::canonical(m_Directory);
            if (!IsPathWithinRoot(m_Directory, root)) {
                m_Error = "Browse directory escaped the declared root";
                return;
            }
            for (const auto& entry : std::filesystem::directory_iterator(m_Directory)) {
                if (m_Files.size() == 4096) {
                    m_Error = "Showing at most 4096 entries; open a smaller folder";
                    break;
                }
                std::error_code code;
                const auto actual = std::filesystem::canonical(entry.path(), code);
                if (code || !IsPathWithinRoot(actual, root))
                    continue;
                const bool directory = entry.is_directory(code);
                if (!code && (directory || entry.is_regular_file(code)))
                    m_Files.push_back(
                        {entry.path(), Tools::AssetPathText(entry.path().filename()), directory});
            }
            std::sort(m_Files.begin(), m_Files.end(), [](const auto& a, const auto& b) {
                return a.directory != b.directory ? a.directory > b.directory : a.name < b.name;
            });
        } catch (const std::exception& exception) {
            m_Error = exception.what();
        }
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
                    {"Content", session->config.contentRoot}};
                for (const auto& [name, path] : session->config.externalSourceRoots)
                    roots.push_back({name, path});
                if (m_Root >= roots.size())
                    m_Root = 0;
                if (m_Directory.empty()) {
                    m_Directory = roots[m_Root].second;
                    Refresh(roots[m_Root].second);
                }
                if (ImGui::BeginCombo(text("content.root", "Read-only root", "ContentRoot").c_str(),
                                      roots[m_Root].first.c_str())) {
                    for (size_t i = 0; i < roots.size(); ++i) {
                        ImGui::PushID(int(i));
                        if (ImGui::Selectable(roots[i].first.c_str(), m_Root == i)) {
                            m_Root = i;
                            m_Directory = roots[i].second;
                            ClearSelection();
                            Refresh(roots[i].second);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextWrapped("%s", Tools::AssetPathText(m_Directory).c_str());
                if (ImGui::Button(text("assets.up", "Up", "ContentUp").c_str()) &&
                    m_Directory != roots[m_Root].second) {
                    m_Directory = m_Directory.parent_path();
                    ClearSelection();
                    Refresh(roots[m_Root].second);
                }
                ImGui::SameLine();
                if (ImGui::Button(text("assets.refresh", "Refresh", "ContentRefresh").c_str()))
                    Refresh(roots[m_Root].second);
                ImGui::SetNextItemWidth(180);
                ImGui::InputText(text("assets.filter", "Filter", "ContentFilter").c_str(), m_Filter.data(),
                                 m_Filter.size());
                std::filesystem::path enter;
                if (ImGui::BeginListBox("##ContentFiles", {0, 170})) {
                    for (size_t i = 0; i < m_Files.size(); ++i) {
                        const auto& entry = m_Files[i];
                        if (entry.name.find(m_Filter.data()) == std::string::npos)
                            continue;
                        ImGui::PushID(int(i));
                        const auto position = ImGui::GetCursorScreenPos();
                        if (ImGui::Selectable("##Source", entry.path == m_Selected,
                                              ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (m_Selected != entry.path) {
                                ClearSelection();
                                m_Selected = entry.path;
                            }
                            if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                enter = entry.path;
                        }
                        const auto display = (entry.directory ? "> " : "  ") + entry.name;
                        ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text),
                                                            display.c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndListBox();
                }
                if (!enter.empty()) {
                    m_Directory = enter;
                    ClearSelection();
                    Refresh(roots[m_Root].second);
                }
                ImGui::BeginDisabled(build.Busy());
                ImGui::InputDouble(
                    text("content.unit_scale", "Additional unit scale", "ImportUnitScale").c_str(),
                    &m_UnitScale, 0, 0, "%.6g");
                ImGui::Combo(text("content.up_axis", "Source up axis", "ImportUpAxis").c_str(), &m_Up,
                             "Auto / source metadata\0Y\0Z\0");
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
                const bool supported = importers.Find(m_Selected) &&
                                       std::any_of(m_Files.begin(), m_Files.end(), [&](const auto& entry) {
                                           return !entry.directory && entry.path == m_Selected;
                                       });
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
