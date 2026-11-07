#pragma once

#include "catpch.h"
#include "Layer/Layer.h"
#include "WorkspaceSettings.h"
#include "Core/Localization/LocalizationCatalog.h"
#include "Panels/ProjectSettingsPanel.h"

namespace Catalpa::Editor {
    class EditorProject;
    class BuildPanel;
    class ContentBrowserPanel;
    class ConsolePanel;
    class DependencyPanel;
    class NetworkDiagnosticsPanel;
    class AudioMixerPanel;
    class WorkspaceEditing;

    // 编辑工作区由宿主应用挂载；只借用引擎服务和既有 ImGui 上下文。
    class EditorWorkspace final : public Layer {
    public:
        // 宿主只传递明确项目路径；空路径继续浏览部署资源，不猜测作者项目位置。
        explicit EditorWorkspace(std::filesystem::path projectFile = {});
        ~EditorWorkspace() override;
        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Deltatime timestep) override;
        void OnEvent(Event&) override;
        void OnImGuiRender() override;

    private:
        // 菜单只排队请求，文件和布局操作在下一次 NewFrame 前执行。
        enum class Action { None, Save, Reload, ResetLayout, OpenProject, CloseProject };
        struct FileEntry {
            std::filesystem::path path;
            std::string name;
            bool directory = false;
        };

        // 文本回退与稳定控件 ID；翻译不可决定窗口和控件的身份。
        const char* Text(const char* key, const char* fallback) const;
        std::string Label(const char* key, const char* fallback, const char* id) const;
        void LoadLanguage();
        void ApplyTheme();
        bool SaveWorkspace();
        void ReloadWorkspace();
        void BuildDefaultLayout();
        void DrawMenu();
        void DrawScene();
        void DrawAssets();
        void DrawInspector();
        void DrawSettings();
        void RefreshFiles();
        // 项目模式的只读浏览限制在已解析内容根；VFS读取还会独立核对文件句柄。
        bool IsInsideProject(const std::filesystem::path& path) const;
        void Report(const char* key, const char* fallback, std::string error = {});

        WorkspaceSettings m_Settings;
        bool m_ShowNetwork = false, m_ShowAudioMixer = false;
        std::unique_ptr<NetworkDiagnosticsPanel> m_Network;
        std::unique_ptr<AudioMixerPanel> m_AudioMixer;
        std::filesystem::path m_ProjectFile;
        std::unique_ptr<EditorProject> m_Project;
        std::unique_ptr<BuildPanel> m_Build;
        std::unique_ptr<ContentBrowserPanel> m_Content;
        std::unique_ptr<ConsolePanel> m_Console;
        std::unique_ptr<DependencyPanel> m_Dependencies;
        std::unique_ptr<WorkspaceEditing> m_Editing;
        ProjectSettingsPanel m_ProjectSettings;
        std::array<char, 2048> m_ProjectPathInput{};
        bool m_OpenProjectPopup = false;
        std::filesystem::path m_ExecutableDirectory;
        std::filesystem::path m_SettingsPath;
        std::filesystem::path m_BrowseDirectory;
        std::filesystem::path m_SelectedFile;
        LocalizationCatalog m_Catalog;
        std::vector<FileEntry> m_Files;
        std::array<char, 128> m_Filter{};
        std::string m_FileError;
        std::string m_Status;
        std::string m_StatusError;
        Action m_Action = Action::None;
        Action m_DiscardAction = Action::None;
        bool m_DiscardExit = false, m_AllowDiscard = false;
        bool m_Attached = false;
        bool m_AutoSave = true;
        bool m_Dirty = false;
        bool m_ResetLayout = false;
        bool m_FrameSeen = false;
        bool m_CloseRequested = false;
        bool m_SavedForExit = false;
    };

}
