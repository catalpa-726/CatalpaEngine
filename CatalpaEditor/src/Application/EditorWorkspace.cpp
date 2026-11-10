#include "Panels/NetworkDiagnosticsPanel.h"
#include "Panels/AudioMixerPanel.h"
#include "catpch.h"
#include "EditorWorkspace.h"
#include "Panels/EditorText.h"
#include "EditorProject.h"
#include "Application/WorkspaceEditing.h"
#include "Panels/BuildPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/FileBrowserView.h"
#include "Panels/ConsolePanel.h"
#include "Panels/DependencyPanel.h"
#include "Platform/WorkspaceFiles.h"
#include "Core/Application.h"
#include "Log/Log.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Catalpa::Editor {
    namespace {
        constexpr const char* DockspaceName = "Catalpa.Workspace.Dockspace";
        // 中央节点现在承载场景图像；侧边节点和保存的分割比例仍由原 DockSpace 管理。
        constexpr ImGuiDockNodeFlags DockFlags = ImGuiDockNodeFlags_None;
        constexpr const char* ChineseLanguageName = "简体中文 (zh-CN)";

        // 复用以 24 px 烘焙的字体图集，让窗口、菜单和弹窗采用同一个显示比例。
        void ApplyWorkspaceFontSize(int pixels)
        {
            ImGui::GetStyle().FontScaleMain = static_cast<float>(pixels) / WorkspaceSettings::DefaultFontSize;
        }

        bool ValidateChineseGlyphs(const LocalizationCatalog& catalog, std::string& error)
        {
            const auto& io = ImGui::GetIO();
            auto* baked = io.FontDefault && io.Fonts->IsBuilt()
                ? io.FontDefault->GetFontBaked(io.FontDefault->LegacySize) : nullptr;
            if (!baked) {
                error = "Workspace font atlas is unavailable.";
                return false;
            }

            // 核对已上传的静态字号；IsGlyphLoaded 只读索引，不按需烘焙，也不把替代字形视作命中。
            const auto checkText = [&](std::string_view key, std::string_view value) {
                const char* current = value.data();
                const char* end = current + value.size();
                while (current < end) {
                    unsigned int codepoint = 0;
                    const int length = ImTextCharFromUtf8(&codepoint, current, end);
                    if (length <= 0 || codepoint == IM_UNICODE_CODEPOINT_INVALID ||
                        !baked->IsGlyphLoaded(static_cast<ImWchar>(codepoint))) {
                        std::ostringstream message;
                        message << "Workspace font atlas lacks a glyph for " << key
                                << " (U+" << std::hex << std::uppercase << codepoint << ").";
                        error = message.str();
                        return false;
                    }
                    current += length;
                }
                return true;
            };
            if (!checkText("language selector", ChineseLanguageName)) return false;
            for (const auto& [key, value] : catalog.Strings())
                if (!checkText(key, value)) return false;
            return true;
        }

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

    EditorWorkspace::EditorWorkspace(std::filesystem::path projectFile, std::filesystem::path settingsFile)
        : Layer("EditorWorkspace"), m_ProjectFile(std::move(projectFile)), m_SettingsPath(std::move(settingsFile)) {}

    EditorWorkspace::~EditorWorkspace() = default;

    const char* EditorWorkspace::Text(const char* key, const char* fallback) const
    {
        return m_Catalog.Text(key, fallback);
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

    bool EditorWorkspace::LoadLanguage()
    {
        LocalizationCatalog next;
        std::string error;
        const auto path =
            m_ExecutableDirectory / "Resources" / "Localization" / (m_Settings.language + ".json");
        if (!next.LoadFile(path, m_Settings.language, error) ||
            (m_Settings.language == "zh-CN" && !ValidateChineseGlyphs(next, error))) {
            CAT_WARN("Language pack rejected; retaining {0}: {1}", m_Catalog.Language(), error);
            m_Settings.language = m_Catalog.Language();
            return false;
        }
        m_Catalog = std::move(next);
        return true;
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
        // 拖出停靠节点即成为独立系统窗口；在场景图像内操作不移动窗口。
        auto& io = ImGui::GetIO();
        m_PreviousNoAutoMerge = std::exchange(io.ConfigViewportsNoAutoMerge, true);
        m_PreviousMoveFromTitleBarOnly = std::exchange(io.ConfigWindowsMoveFromTitleBarOnly, true);
        m_Attached = true;
        ApplyTheme();
        m_AssetBrowser = std::make_unique<FileBrowserView>(0x4153534554000010ULL);
        auto& app = Application::Get();
        m_Editing = std::make_unique<WorkspaceEditing>(app);
        std::string error;
        if (!GetWorkspacePaths(m_ExecutableDirectory, error)) {
            m_AutoSave = false;
            Report("status.paths_failed", "Cannot locate workspace files", error);
            return;
        }
        if (m_SettingsPath.empty()) m_SettingsPath = m_ExecutableDirectory / "Saved" / "Workspace.json";
        m_BrowseDirectory = m_ExecutableDirectory / "Resources";
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
        LoadLanguage();
        ApplyWorkspaceFontSize(m_Settings.editorFontSize);
        if (result == WorkspaceLoadResult::Failed) {
            m_AutoSave = false;
            Report("status.load_failed", "Workspace load failed; automatic saving is disabled", error);
        } else {
            if (!m_Settings.layout.empty()) ImGui::LoadIniSettingsFromMemory(m_Settings.layout.data(), m_Settings.layout.size());
            Report("status.ready", "Workspace ready");
        }
        Application::Get().GetWindow().SetVSync(m_Settings.vsync);
        m_AssetBrowser->Reset();
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
        const bool networkWasVisible = m_Settings.showNetwork;
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
                m_AssetBrowser->Reset();
                m_Settings.showContent = m_Settings.showBuild = true;
                m_Dirty = true;
                Report("status.project_opened", "Project opened");
            }
        } else if (action == Action::CloseProject) {
            std::string error;
            if (m_Editing && !m_Editing->Reset(error))
                Report("status.project_close_failed", "Project close failed", error);
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
                m_AssetBrowser->Reset();
                Report("status.project_closed", "Project closed");
            }
        } else if (action == Action::Reload)
            ReloadWorkspace();
        else if (action == Action::ResetLayout) {
            m_Settings.showScene = m_Settings.showAssets = m_Settings.showInspector = true;
            m_Settings.showSettings = false;
            m_Settings.showHistory = true;
            m_Settings.showContent = m_Settings.showBuild = true;
            m_Settings.showConsole = m_Settings.showDependencies = false;
            m_Settings.showAudioMixer = m_Settings.showNetwork = false;
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
        if (networkWasVisible && !m_Settings.showNetwork && m_Network)
            m_Network->Close(Application::Get());
        if (m_Build) {
            const auto [width, height] = Application::Get().GetWindow().GetFramebufferSize();
            m_Build->Update({width, height});
        }
        if (m_Console)
            m_Console->Update();
        // 切换标签后仍收尾已启动的缩略图任务，使已提交的材质图集及时停止渲染。
        if (m_Content) m_Content->Update();
        if (m_AssetBrowser) m_AssetBrowser->Update();
    }

    void EditorWorkspace::OnDetach()
    {
        if (!m_Attached) return;
        if (!m_SavedForExit && m_AutoSave && m_FrameSeen && ImGui::GetCurrentContext()) SaveWorkspace();
        m_AssetBrowser->Reset();
        if (m_Content) m_Content->Reset();
        m_SelectedFile.clear();
        if (m_Network)
            m_Network->Close(Application::Get());
        m_Network.reset();
        m_AudioMixer.reset();
        m_Editing.reset();
        m_Build.reset();
        m_Content.reset();
        m_Console.reset();
        m_Dependencies.reset();
        m_Project.reset(); // 本工作区任务先取消并等待，再释放作者会话；Runtime调度器仍由Runtime收束。
        if (ImGui::GetCurrentContext()) {
            auto& io = ImGui::GetIO();
            io.ConfigViewportsNoAutoMerge = m_PreviousNoAutoMerge;
            io.ConfigWindowsMoveFromTitleBarOnly = m_PreviousMoveFromTitleBarOnly;
        }
        m_Attached = false;
    }

    void EditorWorkspace::BuildDefaultLayout()
    {
        const auto* viewport = ImGui::GetMainViewport();
        const ImGuiID dockId = ImHashStr(DockspaceName);
        if (viewport->WorkSize.x <= 0 || viewport->WorkSize.y <= 0) return;
        if (!m_ResetLayout && ImGui::DockBuilderGetNode(dockId)) {
            // Add the History pane to a workspace saved before it existed, keeping the user's other splits.
            const auto sceneId = ImHashStr("###Catalpa.Scene");
            const auto historyId = ImHashStr("###Catalpa.History");
            if (!ImGui::FindWindowSettingsByID(historyId) && !ImGui::FindWindowByID(historyId)) {
                const auto* previous = ImGui::FindWindowSettingsByID(sceneId);
                auto* node = previous ? ImGui::DockBuilderGetNode(previous->DockId) : nullptr;
                if (node && node->IsLeafNode() && node->ID != dockId) {
                    const auto history = ImGui::DockBuilderSplitNode(node->ID, ImGuiDir_Down, .29f, nullptr, nullptr);
                    ImGui::DockBuilderDockWindow("###Catalpa.History", history);
                    ImGui::DockBuilderFinish(dockId);
                }
            }
            return;
        }
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace | DockFlags);
        ImGui::DockBuilderSetNodePos(dockId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
        ImGuiID center = dockId;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###Catalpa.Viewport", center);
        const ImGuiID history = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.29f, nullptr, &left);
        ImGui::DockBuilderDockWindow("###Catalpa.Scene", left);
        ImGui::DockBuilderDockWindow("###Catalpa.History", history);
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
            if (m_Editing && ImGui::MenuItem(Label("grove.open", "Open Grove courtyard", "OpenGrove").c_str(), nullptr, false, !m_Editing->CanReturnFromGrove()))
                m_Editing->OpenGroveShowcase();
            if (m_Editing && ImGui::MenuItem(Label("grove.return", "Return to previous scene", "ReturnGrove").c_str(), nullptr, false, m_Editing->CanReturnFromGrove()))
                m_Editing->ReturnFromGrove();
            ImGui::Separator();
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
            m_Dirty |= ImGui::MenuItem(Label("panel.history", "History", "History").c_str(), nullptr, &m_Settings.showHistory);
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
            if (ImGui::MenuItem(Label("panel.audio_mixer", "Audio mixer", "AudioMixerPanel").c_str(), nullptr,
                                &m_Settings.showAudioMixer))
                m_Dirty = true;
            if (ImGui::MenuItem(Label("panel.network", "Loopback network", "NetworkPanel").c_str(), nullptr,
                                &m_Settings.showNetwork)) {
                if (!m_Settings.showNetwork && m_Network)
                    m_Network->Close(Application::Get());
                m_Dirty = true;
            }
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
        if (m_Editing) {
            // 宽窗口居中，窄窗口接在菜单后，避免遮挡已有菜单项。
            const float strip = 4.f * ImGui::GetFrameHeight() + 3.f * ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - strip) * .5f));
            m_Editing->PlayToolbar([this](const char* key, const char* fallback, const char* id) {
                return id ? Label(key, fallback, id) : std::string(Text(key, fallback));
            });
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

    void EditorWorkspace::DrawHistory()
    {
        if (!m_Settings.showHistory) return;
        if (ImGui::Begin(Label("panel.history", "History", "Catalpa.History").c_str(), &m_Settings.showHistory)) {
            const auto* doc = m_Editing ? m_Editing->CurrentDocument() : nullptr;
            if (!doc || doc->History().Size() == 0) {
                ImGui::TextDisabled("%s", Text("history.empty", "No edits yet"));
            } else {
                const auto& history = doc->History();
                for (size_t index = history.Size(); index > 0; --index) {
                    const size_t entry = index - 1;
                    const bool applied = entry < history.Position();
                    if (!applied) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::Text("%zu  %s", index, Text(history.LabelKey(entry), "Edit"));
                    if (!applied) ImGui::PopStyleColor();
                }
            }
        }
        ImGui::End();
    }

    void EditorWorkspace::DrawStatusBar()
    {
        const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::BeginViewportSideBar("###Catalpa.Status", ImGui::GetMainViewport(), ImGuiDir_Down,
                                        ImGui::GetFrameHeight(), flags | ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                const auto* doc = m_Editing ? m_Editing->CurrentDocument() : nullptr;
                if (doc) {
                    std::string name = DisplayPath(doc->AuthorPath().filename());
                    if (name.empty()) {
                        const auto* scene = dynamic_cast<const SceneDocument*>(doc);
                        name = scene && scene->Resolve() ? scene->Resolve()->GetName() : Text("document.untitled", "Untitled");
                    }
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::Separator();
                    if (doc->History().Dirty()) ImGui::TextUnformatted(Text("document.modified", "Modified"));
                    else if (!doc->AuthorPath().empty()) ImGui::TextUnformatted(Text("document.saved", "Saved"));
                    else ImGui::TextUnformatted(Text("document.session", "Session scene"));
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();
    }

    void EditorWorkspace::DrawScene()
    {
        if (!m_Settings.showScene) return;
        if (ImGui::Begin(Label("panel.scene", "Scene", "Catalpa.Scene").c_str(), &m_Settings.showScene)) {
            const bool entitySelected = m_Editing && m_Editing->HasSceneSelection();
            if (ImGui::Selectable(Label("scene.window", "Main Window", "MainWindow").c_str(), !entitySelected && m_SelectedFile.empty())) {
                if (m_Editing) m_Editing->ClearSceneSelection();
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
            ImGui::TextWrapped("%s", Text("workspace.docking", "Drag a tool panel by its tab to float it. Drop it on a docking guide to dock it again."));
        }
        ImGui::End();
    }

    void EditorWorkspace::DrawAssets()
    {
        if (!m_Settings.showAssets) return;
        if (ImGui::Begin(Label("panel.assets", "Assets", "Catalpa.Assets").c_str(), &m_Settings.showAssets)) {
            const auto root = m_Project ? m_Project->GetSession()->config.contentRoot
                                        : m_ExecutableDirectory / "Resources";
            const PanelText text = [this](const char* key, const char* fallback, const char* id) {
                return id ? Label(key, fallback, id) : std::string(Text(key, fallback));
            };
            const auto rootName = m_Project ? Text("browser.content", "Content") : Text("panel.assets", "Assets");
            if (m_AssetBrowser->Draw(root, rootName, m_BrowseDirectory, m_SelectedFile, 0, text) &&
                !m_SelectedFile.empty()) {
                if (m_Editing) m_Editing->ClearSceneSelection();
                m_Settings.showInspector = true;
                m_Dirty = true;
            }
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
                    if (!error) ImGui::Text(UiFormat("ui.bytes", "%llu bytes"), static_cast<unsigned long long>(bytes));
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
            int language = m_Settings.language == "zh-CN" ? 1 : 0;
            const char* names[]{"English (en-US)", ChineseLanguageName};
            if (ImGui::Combo(Label("setting.language", "Language", "WorkspaceLanguage").c_str(), &language,
                             names, 2)) {
                m_Settings.language = language ? "zh-CN" : "en-US";
                if (LoadLanguage()) m_Dirty = true;
            }
            ImGui::TextUnformatted(Text("setting.font_size", "Interface font size"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##WorkspaceFontSize", &m_Settings.editorFontSize,
                             WorkspaceSettings::MinFontSize, WorkspaceSettings::MaxFontSize,
                             "%d px", ImGuiSliderFlags_AlwaysClamp);
            // 拖动时预览，松开后再触发工作区自动保存。
            if (ImGui::IsItemDeactivatedAfterEdit()) m_Dirty = true;
            if (ImGui::Button(Label("setting.reset_font_size", "Restore 24 px", "ResetWorkspaceFontSize").c_str())) {
                m_Settings.editorFontSize = WorkspaceSettings::DefaultFontSize;
                m_Dirty = true;
            }
            ImGui::TextUnformatted(Text("setting.theme", "Theme: Dark Gray / Emerald"));
            ImGui::Separator();
            if (ImGui::Button(Label("action.save", "Save Workspace", "Save").c_str())) m_Action = Action::Save;
            if (ImGui::Button(Label("action.reload", "Reload Workspace", "Reload").c_str())) m_Action = Action::Reload;
            if (ImGui::Button(Label("action.reset", "Reset Layout", "Reset").c_str())) m_Action = Action::ResetLayout;
            ImGui::Separator();
            ImGui::TextWrapped("%s", Text("setting.persistence", "Font size, panel visibility, VSync and layout are saved automatically and on exit. A previous save is kept as Workspace.json.bak."));
            ImGui::TextWrapped("%s", DisplayPath(m_SettingsPath).c_str());
            ImGui::TextWrapped("%s", m_Status.c_str());
            if (m_Project) {
                const bool busy = (m_Build && m_Build->Busy()) ||
                    (m_Editing && (m_Editing->HasDirtyDocuments() || m_Editing->HasPendingWork()));
                m_ProjectSettings.Draw(
                    *m_Project, busy,
                    [this](const char* key, const char* fallback, const char* id) {
                        return id ? Label(key, fallback, id) : std::string(Text(key, fallback));
                    },
                    m_StatusError);
            }
            if (!m_StatusError.empty()) ImGui::TextWrapped("%s", m_StatusError.c_str());
            if (!m_AutoSave) ImGui::TextWrapped("%s", Text("setting.retry", "Automatic saving is disabled. Save Workspace retries explicitly; Reload Workspace keeps current changes if loading fails."));
        }
        ImGui::End();
    }

    void EditorWorkspace::OnImGuiRender()
    {
        if (!m_Attached) return;
        EditorTextScope language(m_Catalog);
        if (m_Editing) m_Editing->SetProject(m_Project ? &m_Project->GetSession()->config : nullptr);
        DrawMenu();
        const auto discardPopup=Label("document.unsaved","Discard unsaved edits?","Catalpa.DiscardEdits");
        if (m_DiscardAction!=Action::None || m_DiscardExit) ImGui::OpenPopup(discardPopup.c_str());
        if (ImGui::BeginPopupModal(discardPopup.c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(Text("document.unsaved_message","This workspace contains unsaved edits."));
            if (ImGui::Button(UiLabel("ui.save_all_and_continue", "Save all and continue").c_str()) && m_Editing && m_Editing->SaveAll()) {
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
        DrawStatusBar();
        BuildDefaultLayout();
        ImGui::DockSpaceOverViewport(ImHashStr(DockspaceName), ImGui::GetMainViewport(), DockFlags);
        const auto visiblePanels = [this] {
            return std::array{m_Settings.showScene, m_Settings.showAssets, m_Settings.showInspector,
                              m_Settings.showSettings, m_Settings.showContent, m_Settings.showBuild,
                              m_Settings.showConsole, m_Settings.showDependencies, m_Settings.showHistory,
                              m_Settings.showAudioMixer, m_Settings.showNetwork};
        };
        const auto before = visiblePanels();
        DrawScene();
        // 切换回实体时解除旧文件选择，下次点击同一文件也会更新属性面板。
        if (!m_SelectedFile.empty() && m_Editing && m_Editing->HasSceneSelection())
            m_SelectedFile.clear();
        DrawHistory();
        DrawAssets();
        DrawInspector();
        DrawSettings();
        const PanelText text = [this](const char* key, const char* fallback, const char* id) {
            return id ? Label(key, fallback, id) : std::string(Text(key, fallback));
        };
        if (m_Settings.showAudioMixer) {
            if (!m_AudioMixer)
                m_AudioMixer = std::make_unique<AudioMixerPanel>();
            if (ImGui::Begin(Label("panel.audio_mixer", "Audio mixer", "Catalpa.AudioMixer").c_str(),
                             &m_Settings.showAudioMixer))
                m_AudioMixer->Draw(Application::Get(), text, m_StatusError);
            ImGui::End();
        }
        if (m_Settings.showNetwork) {
            if (!m_Network)
                m_Network = std::make_unique<NetworkDiagnosticsPanel>();
            if (ImGui::Begin(Label("panel.network", "Loopback network", "Catalpa.Network").c_str(),
                             &m_Settings.showNetwork))
                m_Network->Draw(Application::Get(), text, m_StatusError);
            ImGui::End();
            if (!m_Settings.showNetwork)
                m_Network->Close(Application::Get());
        }
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
        m_Dirty |= before != visiblePanels();
        // 本帧所有窗口绘制结束后再切换，下一帧一次应用滑块或重新加载的字号。
        ApplyWorkspaceFontSize(m_Settings.editorFontSize);
        m_FrameSeen = true;
    }
}
