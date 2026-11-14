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
	class FileBrowserView;
	class ConsolePanel;
	class DependencyPanel;
	class NetworkDiagnosticsPanel;
	class AudioMixerPanel;
	class WorkspaceEditing;

	/* 编辑工作区由宿主应用挂载；只借用引擎服务和既有 ImGui 上下文。 */
	class EditorWorkspace final : public Layer {
	public:
		/**
		 * @brief 用宿主指定的项目和设置文件初始化编辑器工作区。
		 * 宿主只传递明确项目路径；空路径继续浏览部署资源，不猜测作者项目位置。
		 *
		 * @param[in] projectFile 启动时打开的作者项目文件路径，留空时不自动打开项目。
		 * @param[in] settingsFile 工作区设置文件路径；留空时由宿主解析默认位置。
		 */
		explicit EditorWorkspace(std::filesystem::path projectFile = {}, std::filesystem::path settingsFile = {});
		~EditorWorkspace() override;
		/**
		 * @brief 挂接此层并准备其运行所需的资源。
		 */
		void OnAttach() override;
		/**
		 * @brief 在此层移除时结束其活动并释放持有的资源。
		 */
		void OnDetach() override;
		/**
		 * @brief 执行本帧的更新回调。
		 *
		 * @param[in] timestep 帧时间步；此回调未使用该值。
		 */
		void OnUpdate(Deltatime timestep) override;
		/**
		 * @brief 处理传递到此层的事件。
		 *
		 * @param[in,out] event 宿主派发的事件；工作区接管窗口关闭请求时，将其标为已处理。
		 */
		void OnEvent(Event&) override;
		/**
		 * @brief 在当前 ImGui 帧绘制此层的界面。
		 */
		void OnImGuiRender() override;

	private:
		/* 菜单只排队请求，文件和布局操作在下一次 NewFrame 前执行。 */
		enum class Action { None, Save, Reload, ResetLayout, OpenProject, CloseProject };
		const char* Text(const char* key, const char* fallback) const;
		std::string Label(const char* key, const char* fallback, const char* id) const;
		bool LoadLanguage();
		void ApplyTheme();
		bool SaveWorkspace();
		void ReloadWorkspace();
		void BuildDefaultLayout();
		void DrawMenu();
		void DrawScene();
		void DrawHistory();
		void DrawStatusBar();
		void DrawAssets();
		void DrawInspector();
		void DrawSettings();
		void Report(const char* key, const char* fallback, std::string error = {});

		WorkspaceSettings m_Settings;
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
		std::unique_ptr<FileBrowserView> m_AssetBrowser;
		std::string m_Status;
		std::string m_StatusError;
		Action m_Action = Action::None;
		Action m_DiscardAction = Action::None;
		bool m_DiscardExit = false, m_AllowDiscard = false;
		bool m_Attached = false;
		bool m_PreviousNoAutoMerge = false;
		bool m_PreviousMoveFromTitleBarOnly = false;
		bool m_AutoSave = true;
		bool m_Dirty = false;
		bool m_ResetLayout = false;
		bool m_FrameSeen = false;
		bool m_CloseRequested = false;
		bool m_SavedForExit = false;
	};

}
