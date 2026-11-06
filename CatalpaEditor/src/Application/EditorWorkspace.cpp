#include "catpch.h"
#include "EditorWorkspace.h"
#include "EditorProject.h"
#include "Application/WorkspaceEditing.h"
#include "Panels/BuildPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/DependencyPanel.h"
#include "Platform/WorkspaceFiles.h"
#include "Core/Application.h"
#include "Log/Log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Editor {
    namespace {
        constexpr const char* DockspaceName = "Catalpa.Workspace.Dockspace";
        // 中央节点现在承载场景图像；侧边节点和保存的分割比例仍由原 DockSpace 管理。
        constexpr ImGuiDockNodeFlags DockFlags = ImGuiDockNodeFlags_None;

        // 文件路径以 UTF-8 展示；磁盘访问始终保留 Windows 原生宽字符路径。
        std::string DisplayPath(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return { reinterpret_cast<const char*>(text.data()), text.size() };
        }

        // 主题值采用显示空间 sRGB；已有 ImGui Slang 在输出前负责一次解码。
        ImVec4 Color(unsigned int rgb, float alpha = 1.0f)
        {
            return ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f,
                (rgb & 255) / 255.0f, alpha);
        }
    }

    EditorWorkspace::EditorWorkspace(std::filesystem::path projectFile)
        : Layer("EditorWorkspace"), m_ProjectFile(std::move(projectFile)) {}

    EditorWorkspace::~EditorWorkspace() = default;

    const char* EditorWorkspace::Text(const char* key, const char* fallback) const
    {
        const auto found = m_Strings.find(key);
        return found == m_Strings.end() ? fallback : found->second.c_str();
    }

    std::string EditorWorkspace::Label(const char* key, const char* fallback, const char* id) const
    {
        return std::string(Text(key, fallback)) + "###" + id;
    }

    void EditorWorkspace::Report(const char* key, const char* fallback, std::string error)
    {
        m_Status = Text(key, fallback);
        m_StatusError = std::move(error);
        if (!m_StatusError.empty()) m_Settings.showSettings = true;
        if (m_StatusError.empty()) CAT_INFO("{0}", m_Status);
        else CAT_ERROR("{0}: {1}", m_Status, m_StatusError);
    }

    void EditorWorkspace::LoadLanguage()
    {
        m_Strings.clear();
        std::string bytes, error;
        const auto path = m_ExecutableDirectory / "Resources" / "Localization" / "en-US.json";
        if (!ReadWorkspaceText(path, bytes, 64 * 1024, error)) {
            CAT_WARN("Language pack unavailable; using built-in English: {0}", error);
            return;
        }
        // 当前固定字体仅支持英文。限制 JSON 深度，后续中文适配需同时替换字体。
        bool tooDeep = false;
        const auto document = nlohmann::json::parse(bytes, [&tooDeep](int depth, auto, auto&) {
            if (depth > 4) tooDeep = true;
            return depth <= 4;
        }, false);
        if (tooDeep || document.is_discarded() || !document.is_object()
            || document.value("language", nlohmann::json()) != "en-US"
            || !document.contains("strings") || !document["strings"].is_object()) {
            CAT_WARN("Invalid English language pack; using built-in English.");
            return;
        }
        for (const auto& [key, value] : document["strings"].items()) {
            if (!value.is_string()) continue;
            const auto& text = value.get_ref<const std::string&>();
            // 禁止语言包注入 ImGui 隐藏 ID，也避免当前字体无法显示的字符。
            if (text.empty() || text.size() > 2048 || text.find("##") != std::string::npos
                || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; })) continue;
            m_Strings.emplace(key, text);
        }
    }

    void EditorWorkspace::ApplyTheme()
    {
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(12, 12);
        style.FramePadding = ImVec2(8, 5);
        style.ItemSpacing = ImVec2(8, 7);
        style.WindowRounding = 4;
        style.FrameRounding = 3;
        style.PopupRounding = 4;
        style.ScrollbarRounding = 4;
        style.TabRounding = 3;
        style.WindowBorderSize = 1;
        style.FrameBorderSize = 0;
        auto* colors = style.Colors;
        colors[ImGuiCol_Text] = Color(0xE5E7E6);
        colors[ImGuiCol_TextDisabled] = Color(0x929795);
        colors[ImGuiCol_WindowBg] = Color(0x272727);
        colors[ImGuiCol_ChildBg] = Color(0x242424);
        colors[ImGuiCol_PopupBg] = Color(0x303030);
        colors[ImGuiCol_Border] = Color(0x454545);
        colors[ImGuiCol_BorderShadow] = Color(0x000000, 0);
        colors[ImGuiCol_FrameBg] = Color(0x353535);
        colors[ImGuiCol_FrameBgHovered] = Color(0x454545);
        colors[ImGuiCol_FrameBgActive] = Color(0x505050);
        colors[ImGuiCol_TitleBg] = Color(0x202020);
        colors[ImGuiCol_TitleBgActive] = Color(0x303733);
        colors[ImGuiCol_TitleBgCollapsed] = Color(0x202020);
        colors[ImGuiCol_MenuBarBg] = Color(0x222222);
        colors[ImGuiCol_ScrollbarBg] = Color(0x222222);
        colors[ImGuiCol_ScrollbarGrab] = Color(0x505050);
        colors[ImGuiCol_ScrollbarGrabHovered] = Color(0x626262);
        colors[ImGuiCol_ScrollbarGrabActive] = Color(0x35B681);
        colors[ImGuiCol_CheckMark] = Color(0x35B681);
        colors[ImGuiCol_CheckboxSelectedBg] = Color(0x34463D);
        colors[ImGuiCol_SliderGrab] = Color(0x35B681);
        colors[ImGuiCol_SliderGrabActive] = Color(0x62CBA0);
        colors[ImGuiCol_Button] = Color(0x3B3B3B);
        colors[ImGuiCol_ButtonHovered] = Color(0x345B49);
        colors[ImGuiCol_ButtonActive] = Color(0x287C59);
        colors[ImGuiCol_Header] = Color(0x34463D);
        colors[ImGuiCol_HeaderHovered] = Color(0x345B49);
        colors[ImGuiCol_HeaderActive] = Color(0x287C59);
        colors[ImGuiCol_Separator] = Color(0x454545);
        colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_SeparatorActive] = Color(0x35B681);
        colors[ImGuiCol_ResizeGrip] = Color(0x35B681, 0.2f);
        colors[ImGuiCol_ResizeGripHovered] = Color(0x35B681, 0.65f);
        colors[ImGuiCol_ResizeGripActive] = Color(0x35B681);
        colors[ImGuiCol_InputTextCursor] = Color(0x35B681);
        colors[ImGuiCol_Tab] = Color(0x292929);
        colors[ImGuiCol_TabHovered] = Color(0x345B49);
        colors[ImGuiCol_TabSelected] = Color(0x383838);
        colors[ImGuiCol_TabSelectedOverline] = Color(0x35B681);
        colors[ImGuiCol_TabDimmed] = Color(0x242424);
        colors[ImGuiCol_TabDimmedSelected] = Color(0x303030);
        colors[ImGuiCol_TabDimmedSelectedOverline] = Color(0x56806D);
        colors[ImGuiCol_DockingPreview] = Color(0x35B681, 0.5f);
        colors[ImGuiCol_DockingEmptyBg] = Color(0x272727);
        colors[ImGuiCol_TableHeaderBg] = Color(0x303030);
        colors[ImGuiCol_TableBorderStrong] = Color(0x454545);
        colors[ImGuiCol_TableBorderLight] = Color(0x353535);
        colors[ImGuiCol_TextLink] = colors[ImGuiCol_NavCursor] = Color(0x35B681);
        colors[ImGuiCol_TextSelectedBg] = Color(0x35B681, 0.3f);
        colors[ImGuiCol_DragDropTarget] = Color(0x35B681);
    }

    void EditorWorkspace::OnAttach()
    {
        CAT_ASSERT(ImGui::GetCurrentContext(), "Workspace requires the application's ImGui context.");
        IMGUI_CHECKVERSION();
        m_Attached = true;
        ApplyTheme();
        auto& app = Application::Get();
        m_Editing = std::make_unique<WorkspaceEditing>(app);
        std::string error;
        if (!GetWorkspacePaths(m_ExecutableDirectory, error)) {
            m_AutoSave = false;
            Report("status.paths_failed", "Cannot locate workspace files", error);
            return;
        }
        m_SettingsPath = m_ExecutableDirectory / "Saved" / "Workspace.json";
        m_BrowseDirectory = m_ExecutableDirectory / "Resources";
        LoadLanguage();
        m_Build = std::make_unique<BuildPanel>(app.GetJobs(), app.GetAssets(), app.GetScenes());
        m_Content = std::make_unique<ContentBrowserPanel>();
        m_Console = std::make_unique<ConsolePanel>();
        m_Dependencies = std::make_unique<DependencyPanel>();
        if (!m_ProjectFile.empty()) {
            auto project = std::make_unique<EditorProject>();
            if (!project->Load(m_ProjectFile, error)) {
                m_AutoSave = false;
                Report("status.project_failed", "Project open failed", error);
                Application::Get().RequestClose(1);
                return;
            }
            const auto& session = *project->GetSession();
            m_BrowseDirectory = session.config.contentRoot;
            CAT_INFO("Project opened: {0} ({1}), {2} registered assets.", session.config.name,
                session.config.id.ToString(), session.assets.GetRecords().size());
            for (const auto& diagnostic : session.diagnostics) CAT_WARN("Project: {0}", diagnostic);
            m_Project = std::move(project);
        }
        const auto result = LoadWorkspaceSettings(m_SettingsPath, m_Settings, error);
        if (result == WorkspaceLoadResult::Failed) {
            m_AutoSave = false;
            Report("status.load_failed", "Workspace load failed; automatic saving is disabled", error);
        } else {
            if (!m_Settings.layout.empty()) ImGui::LoadIniSettingsFromMemory(m_Settings.layout.data(), m_Settings.layout.size());
            Report("status.ready", "Workspace ready");
        }
        Application::Get().GetWindow().SetVSync(m_Settings.vsync);
        RefreshFiles();
    }

    bool EditorWorkspace::SaveWorkspace()
    {
        if (m_SettingsPath.empty() || !m_FrameSeen || m_ResetLayout) return false;
        size_t size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&size);
        m_Settings.layout.assign(ini, size);
        m_Settings.vsync = Application::Get().GetWindow().IsVSync();
        std::string error;
        if (!SaveWorkspaceSettings(m_SettingsPath, m_Settings, error)) {
            m_AutoSave = false; // 避免每帧重复写入失败；用户可通过 Save Workspace 重试。
            Report("status.save_failed", "Workspace save failed; automatic saving is disabled", error);
            return false;
        }
        ImGui::GetIO().WantSaveIniSettings = false;
        m_Dirty = false;
        m_AutoSave = true;
        m_Status = Text("status.saved", "Workspace saved");
        m_StatusError.clear();
        return true;
    }

    void EditorWorkspace::ReloadWorkspace()
    {
        WorkspaceSettings loaded;
        std::string error;
        const auto result = LoadWorkspaceSettings(m_SettingsPath, loaded, error);
        if (result != WorkspaceLoadResult::Loaded) {
            // 失败保留屏幕上的状态，并禁止退出时覆盖用户希望重新读取的文件。
            m_AutoSave = false;
            if (result == WorkspaceLoadResult::Missing) error = "No saved workspace exists yet.";
            Report("status.load_failed", "Workspace load failed; automatic saving is disabled", error);
            return;
        }
        m_Settings = std::move(loaded);
        ImGui::ClearIniSettings();
        if (!m_Settings.layout.empty()) ImGui::LoadIniSettingsFromMemory(m_Settings.layout.data(), m_Settings.layout.size());
        m_ResetLayout = m_Settings.layout.empty();
        Application::Get().GetWindow().SetVSync(m_Settings.vsync);
        m_Dirty = false;
        m_AutoSave = true;
        LoadLanguage();
        Report("status.loaded", "Workspace reloaded");
    }

    void EditorWorkspace::OnEvent(Event& event)
    {
        if (event.GetEventType() == EventType::WindowClose && m_Attached && !m_SavedForExit) {
            event.Handled = true;
            m_CloseRequested = true;
        }
    }

    void EditorWorkspace::OnUpdate(Deltatime)
    {
        if (!m_Attached) return;
        Action action = std::exchange(m_Action, Action::None);
        const bool destructive = action == Action::OpenProject || action == Action::CloseProject || m_CloseRequested;
        if (destructive && !std::exchange(m_AllowDiscard,false) && m_Editing && m_Editing->HasDirtyDocuments()) {
            m_DiscardAction=action; m_DiscardExit=m_CloseRequested;
            m_CloseRequested=false; action=Action::None;
        }
        if (action == Action::OpenProject) {
            std::string error;
            auto candidate = std::make_unique<EditorProject>();
            if (!candidate->Load(std::filesystem::u8path(m_ProjectPathInput.data()), error))
                Report("status.project_failed", "Project open failed", error);
            else if (m_Editing && !m_Editing->Reset(error))
                Report("status.project_failed", "Project open failed", error);
            else {
                if (m_Build)
                    m_Build->CloseProject();
                m_Project = std::move(candidate);
                m_ProjectSettings.Reset();
                m_ProjectFile = m_Project->GetSession()->config.projectFile;
                m_BrowseDirectory = m_Project->GetSession()->config.contentRoot;
                m_SelectedFile.clear();
                if (m_Content)
                    m_Content->Reset();
                RefreshFiles();
                m_Settings.showContent = m_Settings.showBuild = true;
                m_Dirty = true;
                Report("status.project_opened", "Project opened");
            }
        } else if (action == Action::CloseProject) {
            std::string error;
            if (m_Editing && !m_Editing->Reset(error))
                Report("status.project_failed", "Project close failed", error);
            else {
                if (m_Build)
                    m_Build->CloseProject();
                m_Project.reset();
                m_ProjectSettings.Reset();
                m_ProjectFile.clear();
                m_SelectedFile.clear();
                if (m_Content)
                    m_Content->Reset();
                m_BrowseDirectory = m_ExecutableDirectory / "Resources";
                RefreshFiles();
                Report("status.project_closed", "Project closed");
            }
        } else if (action == Action::Reload)
            ReloadWorkspace();
        else if (action == Action::ResetLayout) {
            m_Settings.showScene = m_Settings.showAssets = m_Settings.showInspector = true;
            m_Settings.showSettings = false;
            m_Settings.showContent = m_Settings.showBuild = true;
            m_Settings.showConsole = m_Settings.showDependencies = false;
            m_ResetLayout = true;
            m_Dirty = true;
            Report("status.reset", "Default layout restored");
        } else if (action == Action::Save || m_CloseRequested) {
            const bool saved = (!m_FrameSeen || m_SettingsPath.empty()) ? true : SaveWorkspace();
            if (m_CloseRequested && saved) {
                m_SavedForExit = true;
                Application::Get().RequestClose();
            }
            m_CloseRequested = false;
        } else if (m_AutoSave && !m_ResetLayout && m_FrameSeen
            && (m_Dirty || ImGui::GetIO().WantSaveIniSettings)) SaveWorkspace();
        if (m_Build) {
            const auto [width, height] = Application::Get().GetWindow().GetFramebufferSize();
            m_Build->Update({width, height});
        }
        if (m_Console)
            m_Console->Update();
    }

    void EditorWorkspace::OnDetach()
    {
        if (!m_Attached) return;
        if (!m_SavedForExit && m_AutoSave && m_FrameSeen && ImGui::GetCurrentContext()) SaveWorkspace();
        m_Files.clear();
        m_SelectedFile.clear();
        m_Editing.reset();
        m_Build.reset();
        m_Content.reset();
        m_Console.reset();
        m_Dependencies.reset();
        m_Project.reset(); // 本工作区任务先取消并等待，再释放作者会话；Runtime调度器仍由Runtime收束。
        m_Attached = false;
    }

    void EditorWorkspace::BuildDefaultLayout()
    {
        const auto* viewport = ImGui::GetMainViewport();
        const ImGuiID dockId = ImHashStr(DockspaceName);
        if (viewport->WorkSize.x <= 0 || viewport->WorkSize.y <= 0) return;
        if (!m_ResetLayout && ImGui::DockBuilderGetNode(dockId)) return;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace | DockFlags);
        ImGui::DockBuilderSetNodePos(dockId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
        ImGuiID center = dockId;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###Catalpa.Viewport", center);
        ImGui::DockBuilderDockWindow("###Catalpa.Scene", left);
        ImGui::DockBuilderDockWindow("###Catalpa.Inspector", right);
        ImGui::DockBuilderDockWindow("###Catalpa.Settings", right);
        ImGui::DockBuilderDockWindow("###Catalpa.Assets", bottom);
        ImGui::DockBuilderDockWindow("###Catalpa.Content", bottom);
        ImGui::DockBuilderDockWindow("###Catalpa.Console", bottom);
        ImGui::DockBuilderDockWindow("###Catalpa.Build", right);
        ImGui::DockBuilderDockWindow("###Catalpa.Dependencies", right);
        ImGui::DockBuilderFinish(dockId);
        m_ResetLayout = false;
        m_Dirty = true;
    }

    void EditorWorkspace::DrawMenu()
    {
        if (!ImGui::BeginMainMenuBar()) return;
        ImGui::TextUnformatted("CATALPA");
        if (ImGui::BeginMenu(Label("menu.file", "File", "File").c_str())) {
            if (ImGui::MenuItem(Label("action.open_project", "Open Project...", "OpenProject").c_str()))
                m_OpenProjectPopup = true;
            if (ImGui::MenuItem(Label("action.close_project", "Close Project", "CloseProject").c_str(),
                                nullptr, false, bool(m_Project)))
                m_Action = Action::CloseProject;
            if (ImGui::MenuItem(Label("action.import", "Import / Build Source", "ImportSource").c_str())) {
                m_Settings.showContent = m_Settings.showBuild = true;
                m_Dirty = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Label("action.save", "Save Workspace", "Save").c_str())) m_Action = Action::Save;
            if (ImGui::MenuItem(Label("action.reload", "Reload Workspace", "Reload").c_str())) m_Action = Action::Reload;
            ImGui::Separator();
            if (ImGui::MenuItem(Label("action.exit", "Save and Exit", "Exit").c_str())) m_CloseRequested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Label("menu.view", "View", "View").c_str())) {
            m_Dirty |= ImGui::MenuItem(Label("panel.scene", "Scene", "Scene").c_str(), nullptr, &m_Settings.showScene);
            m_Dirty |= ImGui::MenuItem(Label("panel.assets", "Assets", "Assets").c_str(), nullptr, &m_Settings.showAssets);
            m_Dirty |= ImGui::MenuItem(Label("panel.inspector", "Inspector", "Inspector").c_str(), nullptr,
                                       &m_Settings.showInspector);
            m_Dirty |= ImGui::MenuItem(Label("panel.settings", "Settings", "Settings").c_str(), nullptr,
                                       &m_Settings.showSettings);
            m_Dirty |= ImGui::MenuItem(Label("panel.content", "Content Browser", "Content").c_str(), nullptr,
                                       &m_Settings.showContent);
            m_Dirty |= ImGui::MenuItem(Label("panel.build", "Build", "Build").c_str(), nullptr,
                                       &m_Settings.showBuild);
            m_Dirty |= ImGui::MenuItem(Label("panel.console", "Console", "Console").c_str(), nullptr,
                                       &m_Settings.showConsole);
            m_Dirty |= ImGui::MenuItem(Label("panel.dependencies", "Dependencies", "Dependencies").c_str(),
                                       nullptr, &m_Settings.showDependencies);
            ImGui::Separator();
            if (ImGui::MenuItem(Label("action.reset", "Reset Layout", "Reset").c_str())) m_Action = Action::ResetLayout;
            ImGui::EndMenu();
        }
        if (m_Editing) {
            const PanelText text = [this](const char* key,const char* fallback,const char* id) {
                return id ? Label(key,fallback,id) : std::string(Text(key,fallback));
            };
            m_Editing->Menu(text);
        }
        if (ImGui::BeginMenu(Label("menu.settings", "Settings", "SettingsMenu").c_str())) {
            bool vsync = Application::Get().GetWindow().IsVSync();
            if (ImGui::MenuItem(Label("setting.vsync", "VSync", "VSync").c_str(), nullptr, &vsync)) {
                Application::Get().GetWindow().SetVSync(vsync);
                m_Settings.vsync = vsync;
                m_Dirty = true;
            }
            if (ImGui::MenuItem(Label("action.preferences", "Preferences", "Preferences").c_str())) {
                m_Settings.showSettings = true;
                m_Dirty = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
        if (m_OpenProjectPopup) {
            ImGui::OpenPopup(Label("panel.open_project", "Open Project", "Catalpa.OpenProject").c_str());
            m_OpenProjectPopup = false;
        }
        if (ImGui::BeginPopupModal(Label("panel.open_project", "Open Project", "Catalpa.OpenProject").c_str(),
                                   nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "%s", Text("project.path_hint", "Enter the absolute path of a .catalpaproj author project."));
            ImGui::SetNextItemWidth(650);
            ImGui::InputText(Label("project.path", "Project file", "ProjectPath").c_str(),
                             m_ProjectPathInput.data(), m_ProjectPathInput.size());
            if (ImGui::Button(Label("project.open", "Open", "ConfirmOpenProject").c_str())) {
                m_Action = Action::OpenProject;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Label("project.cancel", "Cancel", "CancelOpenProject").c_str()))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void EditorWorkspace::DrawScene()
    {
        if (!m_Settings.showScene) return;
        if (ImGui::Begin(Label("panel.scene", "Scene", "Catalpa.Scene").c_str(), &m_Settings.showScene)) {
            if (ImGui::Selectable(Label("scene.window", "Main Window", "MainWindow").c_str(), m_SelectedFile.empty())) {
                m_SelectedFile.clear();
                m_Settings.showInspector = true;
                m_Dirty = true;
            }
            ImGui::Separator();
            if (const auto* scene = Application::Get().GetScenes().GetActive())
                ImGui::TextWrapped("%s", scene->GetName().c_str());
            if (m_Editing) {
                const PanelText text = [this](const char* key,const char* fallback,const char* id) {
                    return id ? Label(key,fallback,id) : std::string(Text(key,fallback));
                };
                m_Editing->SceneTree(text);
            }
            ImGui::Spacing();
            ImGui::TextWrapped("%s", Text("workspace.docking", "Drag panel title bars to arrange the workspace. Use View to reopen panels."));
        }
        ImGui::End();
    }

    bool EditorWorkspace::IsInsideProject(const std::filesystem::path& path) const
    {
        if (!m_Project) return true;
        std::error_code error;
        const auto actual = std::filesystem::canonical(path, error);
        if (error) return false;
        const auto& root = m_Project->GetSession()->config.contentRoot;
        // 浏览范围用真实路径精确比较，避免Windows可选大小写敏感目录的同名兄弟逃逸。
        auto component = actual.begin();
        for (const auto& expected : root) {
            if (expected.empty()) continue;
            if (component == actual.end() || *component != expected) return false;
            ++component;
        }
        return true;
    }

    void EditorWorkspace::RefreshFiles()
    {
        m_Files.clear();
        m_FileError.clear();
        if (m_BrowseDirectory.empty()) return;
        if (!IsInsideProject(m_BrowseDirectory)) {
            m_FileError = Text("assets.outside_project", "Directory is outside the project content root.");
            return;
        }
        std::error_code error;
        std::filesystem::directory_iterator it(m_BrowseDirectory, error), end;
        size_t visited = 0;
        while (!error && it != end && visited++ < 512) {
            const auto path = it->path();
            const bool directory = it->is_directory(error);
            if (error) break;
            if (IsInsideProject(path)) m_Files.push_back({ path, DisplayPath(path.filename()), directory });
            it.increment(error);
        }
        if (error) m_FileError = error.message();
        else if (it != end) m_FileError = Text("assets.limit", "Showing the first 512 entries; open a smaller folder to see more.");
        std::sort(m_Files.begin(), m_Files.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.directory != b.directory) return a.directory > b.directory;
            return a.name < b.name;
        });
    }

    void EditorWorkspace::DrawAssets()
    {
        if (!m_Settings.showAssets) return;
        if (ImGui::Begin(Label("panel.assets", "Assets", "Catalpa.Assets").c_str(), &m_Settings.showAssets)) {
            ImGui::TextUnformatted(Text("assets.read_only", "Resource files (read-only)"));
            if (m_Project) {
                ImGui::Text("%s: %s", Text("assets.project", "Project"), m_Project->GetSession()->config.name.c_str());
            }
            ImGui::TextWrapped("%s", DisplayPath(m_BrowseDirectory).c_str());
            if (ImGui::Button(Label("assets.up", "Up", "Up").c_str()) && m_BrowseDirectory.has_parent_path()
                && IsInsideProject(m_BrowseDirectory.parent_path())) {
                m_BrowseDirectory = m_BrowseDirectory.parent_path();
                RefreshFiles();
            }
            ImGui::SameLine();
            if (ImGui::Button(Label("assets.refresh", "Refresh", "Refresh").c_str())) RefreshFiles();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            ImGui::InputText(Label("assets.filter", "Filter", "Filter").c_str(), m_Filter.data(), m_Filter.size());
            if (!m_FileError.empty()) ImGui::TextWrapped("%s", m_FileError.c_str());
            ImGui::BeginChild("FileList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            std::filesystem::path enterDirectory;
            for (const auto& entry : m_Files) {
                if (entry.name.find(m_Filter.data()) == std::string::npos) continue;
                // 文件名只用于显示，不让其中的 ## 或翻译改变控件 ID。
                const std::string pathId = DisplayPath(entry.path);
                ImGui::PushID(static_cast<int>(ImHashData(pathId.data(), pathId.size())));
                const ImVec2 textPosition = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##File", m_SelectedFile == entry.path, ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
                    m_SelectedFile = entry.path;
                    m_Settings.showInspector = true;
                    m_Dirty = true;
                    if (entry.directory && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                        || ImGui::IsKeyPressed(ImGuiKey_Enter))) enterDirectory = entry.path;
                }
                const std::string displayName = (entry.directory ? "> " : "  ") + entry.name;
                ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), displayName.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (!enterDirectory.empty()) { m_BrowseDirectory = enterDirectory; RefreshFiles(); }
        }
        ImGui::End();
    }

    void EditorWorkspace::DrawInspector()
    {
        if (!m_Settings.showInspector) return;
        if (ImGui::Begin(Label("panel.inspector", "Inspector", "Catalpa.Inspector").c_str(), &m_Settings.showInspector)) {
            const PanelText label = [this](const char* key,const char* fallback,const char* id) {
                return id ? Label(key,fallback,id) : std::string(Text(key,fallback));
            };
            if (m_Editing && m_Editing->Inspect(label)) {
                // 活动选择已由 Inspector 的类型适配器绘制。
            } else if (m_SelectedFile.empty()) {
                const auto& window = Application::Get().GetWindow();
                ImGui::TextUnformatted(Text("scene.window", "Main Window"));
                ImGui::Separator();
                ImGui::TextUnformatted(Text("inspector.logical", "Window size"));
                ImGui::Text("%u x %u", window.GetWidth(), window.GetHeight());
                const auto [width, height] = window.GetFramebufferSize();
                ImGui::TextUnformatted(Text("inspector.framebuffer", "Framebuffer size"));
                ImGui::Text("%u x %u", width, height);
                ImGui::TextUnformatted(Text("inspector.framerate", "UI frame rate"));
                ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
            } else {
                ImGui::TextWrapped("%s", DisplayPath(m_SelectedFile).c_str());
                std::error_code error;
                if (std::filesystem::is_regular_file(m_SelectedFile, error)) {
                    const auto bytes = std::filesystem::file_size(m_SelectedFile, error);
                    if (!error) ImGui::Text("%llu bytes", static_cast<unsigned long long>(bytes));
                }
                if (error) ImGui::TextWrapped("%s", error.message().c_str());
                ImGui::Separator();
                ImGui::TextWrapped("%s", Text("inspector.file_scope", "Open an author project and use Content Browser to import supported source files."));
            }
        }
        ImGui::End();
    }

    void EditorWorkspace::DrawSettings()
    {
        if (!m_Settings.showSettings) return;
        if (ImGui::Begin(Label("panel.settings", "Settings", "Catalpa.Settings").c_str(), &m_Settings.showSettings)) {
            bool vsync = Application::Get().GetWindow().IsVSync();
            if (ImGui::Checkbox(Label("setting.vsync", "VSync", "VSync").c_str(), &vsync)) {
                Application::Get().GetWindow().SetVSync(vsync);
                m_Settings.vsync = vsync;
                m_Dirty = true;
            }
            ImGui::TextUnformatted(Text("setting.language", "Language: English (en-US)"));
            ImGui::TextUnformatted(Text("setting.theme", "Theme: Dark Gray / Emerald"));
            ImGui::Separator();
            if (ImGui::Button(Label("action.save", "Save Workspace", "Save").c_str())) m_Action = Action::Save;
            if (ImGui::Button(Label("action.reload", "Reload Workspace", "Reload").c_str())) m_Action = Action::Reload;
            if (ImGui::Button(Label("action.reset", "Reset Layout", "Reset").c_str())) m_Action = Action::ResetLayout;
            ImGui::Separator();
            ImGui::TextWrapped("%s", Text("setting.persistence", "Panel visibility, VSync and layout are saved automatically and on exit. A previous save is kept as Workspace.json.bak."));
            ImGui::TextWrapped("%s", DisplayPath(m_SettingsPath).c_str());
            ImGui::TextWrapped("%s", m_Status.c_str());
            if (m_Project) {
                const bool busy = (m_Build && m_Build->Busy()) ||
                    (m_Editing && (m_Editing->HasDirtyDocuments() || m_Editing->HasPendingWork()));
                m_ProjectSettings.Draw(*m_Project, busy, m_StatusError);
            }
            if (!m_StatusError.empty()) ImGui::TextWrapped("%s", m_StatusError.c_str());
            if (!m_AutoSave) ImGui::TextWrapped("%s", Text("setting.retry", "Automatic saving is disabled. Save Workspace retries explicitly; Reload Workspace keeps current changes if loading fails."));
        }
        ImGui::End();
    }

    void EditorWorkspace::OnImGuiRender()
    {
        if (!m_Attached) return;
        if (m_Editing) m_Editing->SetProject(m_Project ? &m_Project->GetSession()->config : nullptr);
        DrawMenu();
        const auto discardPopup=Label("document.unsaved","Discard unsaved edits?","Catalpa.DiscardEdits");
        if (m_DiscardAction!=Action::None || m_DiscardExit) ImGui::OpenPopup(discardPopup.c_str());
        if (ImGui::BeginPopupModal(discardPopup.c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(Text("document.unsaved_message","This workspace contains unsaved edits."));
            if (ImGui::Button("Save all and continue") && m_Editing && m_Editing->SaveAll()) {
                m_AllowDiscard = true;
                m_Action = std::exchange(m_DiscardAction, Action::None);
                m_CloseRequested = std::exchange(m_DiscardExit, false);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(Label("document.discard_changes","Discard and continue","DiscardEdits").c_str())) {
                if (m_Editing) m_Editing->DiscardRecoveryCopies();
                m_AllowDiscard=true;
                m_Action=std::exchange(m_DiscardAction,Action::None);
                m_CloseRequested=std::exchange(m_DiscardExit,false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Label("document.keep","Keep editing","KeepEdits").c_str())) {
                m_DiscardAction=Action::None; m_DiscardExit=false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        BuildDefaultLayout();
        ImGui::DockSpaceOverViewport(ImHashStr(DockspaceName), ImGui::GetMainViewport(), DockFlags);
        const std::array before{m_Settings.showScene,    m_Settings.showAssets,      m_Settings.showInspector,
                                m_Settings.showSettings, m_Settings.showContent,     m_Settings.showBuild,
                                m_Settings.showConsole,  m_Settings.showDependencies};
        DrawScene();
        DrawAssets();
        DrawInspector();
        DrawSettings();
        const PanelText text = [this](const char* key, const char* fallback, const char* id) {
            return id ? Label(key, fallback, id) : std::string(Text(key, fallback));
        };
        if (m_Content && m_Build)
            m_Content->Draw(m_Settings.showContent, m_Project ? m_Project->GetSession() : nullptr,
                            m_ExecutableDirectory / "Resources" / "Shaders", *m_Build, text);
        if (m_Build)
            m_Build->Draw(m_Settings.showBuild, text);
        if (m_Console)
            m_Console->Draw(m_Settings.showConsole, text);
        if (m_Editing) {
            if (m_Build) {
                const auto asset = m_Build->TakeOpenAsset();
                if (asset.HasValue()) m_Editing->OpenAsset(asset,*m_Build);
            }
            const auto dockId = ImHashStr(DockspaceName);
            const auto* center = ImGui::DockBuilderGetCentralNode(dockId);
            m_Editing->Draw(text, m_Build.get(), center ? center->ID : dockId);
        }
        if (m_Dependencies && m_Build)
            m_Dependencies->Draw(m_Settings.showDependencies, m_Build->Result(), text);
        const std::array after{m_Settings.showScene,    m_Settings.showAssets,      m_Settings.showInspector,
                               m_Settings.showSettings, m_Settings.showContent,     m_Settings.showBuild,
                               m_Settings.showConsole,  m_Settings.showDependencies};
        m_Dirty |= before != after;
        m_FrameSeen = true;
    }
}
