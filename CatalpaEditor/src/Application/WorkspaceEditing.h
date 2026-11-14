#pragma once
#include "AssetEditors/AssetEditorRegistry.h"
#include "Documents/DocumentManager.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Viewport/PickingController.h"
#include "Viewport/TransformGizmo.h"
#include "Viewport/EditorCameraController.h"
#include "Scene/SceneAsset.h"

namespace Catalpa {
	class AssetDependencyGraph;
}
namespace Catalpa::Editor {
	class BuildPanel;

	/* 工作区的文档和交互生命周期。宿主通过引用传入，SceneManager 与 GPU 资源均为借用。 */
	class WorkspaceEditing {
	public:
		explicit WorkspaceEditing(Application& app);

		~WorkspaceEditing();
		/**
		 * @brief 停止 Play 并清理文档选择、拾取和编辑会话。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Reset(std::string& error);
		/**
		 * @brief 绘制当前场景的实体层级，并处理实体选择与层级编辑。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void SceneTree(const PanelText&);
		bool HasSceneSelection();
		/**
		 * @brief 清除编辑器保存的场景实体选择。
		 */
		void ClearSceneSelection();
		/**
		 * @brief 为当前选择绘制对应属性，并报告是否提供了检查视图。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 *
		 * @return 已为当前选中实体绘制属性或显示运行模式提示时为 true；没有可检查的选择时为 false。
		 */
		bool Inspect(const PanelText&);
		/**
		 * @brief 更新编辑文档并绘制场景视口及文档编辑界面。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[in] build 提供最近一次构建结果和产物目录的面板。
		 * @param[in] centerDockId 主窗口中央停靠节点的标识。
		 */
		void Draw(const PanelText&, BuildPanel*, uint32_t centerDockId);
		/**
		 * @brief 打开资产。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] build 提供最近一次构建结果和产物目录的面板。
		 */
		void OpenAsset(AssetId, const BuildPanel&);
		/**
		 * @brief 绘制文档和编辑菜单，转发历史记录与编辑操作。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Menu(const PanelText&);
		/**
		 * @brief 绘制运行、暂停、单步与停止控件。
		 *
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void PlayToolbar(const PanelText&);
		/**
		 * @brief 在视口指定位置发起实体拾取。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] frame 当前已呈现的视口快照，用于校验拾取请求对应的场景及帧序列。
		 * @param[in] x 视口渲染目标内要拾取的水平像素。
		 * @param[in] y 视口渲染目标内要拾取的垂直像素。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool PickViewport(SceneDocument&, const RenderSurfaceSnapshot&, uint32_t x, uint32_t y, std::string& error);
		Document* CurrentDocument() noexcept { return m_Documents.Active(); }
		const Document* CurrentDocument() const noexcept { return m_Documents.Active(); }
		bool HasDirtyDocuments() const;
		bool HasPendingWork() const noexcept {
			return m_PlayDocument.HasValue() || m_PendingScene.has_value() || m_PendingAuthor.has_value();
		}
		/**
		 * @brief 设置项目。
		 *
		 * @param[in] config 当前作者项目的目录和构建设置，传入空指针表示关闭项目。
		 */
		void SetProject(const Tools::ProjectConfig*);
		/**
		 * @brief 保存全部。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool SaveAll();
		void DiscardRecoveryCopies();
		/**
		 * @brief 在编辑器中打开 Grove 庭院展示场景。
		 */
		void OpenGroveShowcase();
		/**
		 * @brief 从庭院展示返回先前打开的编辑场景。
		 */
		void ReturnFromGrove();
		bool CanReturnFromGrove() const noexcept { return m_PreGroveScene.HasValue(); }

	private:
		SceneDocument* ActiveScene();
		void ActivateAsset(AssetDocument&);
		void DrawViewport(SceneDocument*, const PanelText&, uint32_t centerDockId);
		EditorCameraInput ViewportInput(bool hovered, bool gizmoBusy);
		bool RoutePlayUIInput(float x, float y, float width, float height, Extent2D extent, bool hovered, bool focused);
		void BlurPlayUI();
		bool SaveDocument(Document&, bool saveAs = false);
		bool LoadProjectCatalog();
		bool OpenAuthor(const std::filesystem::path&, std::string text = {}, std::string type = {},
		    const std::filesystem::path& recovery = {}, const AssetDependencyGraph* loadGraph = nullptr);
		bool InstallAuthorScene(
		    const SceneAsset&, const std::filesystem::path&, const std::filesystem::path& recovery = {});
		void RemoveRecovery(StableId);
		void RetainRecovery(const std::filesystem::path&);
		void CancelPendingOpen();
		void DocumentMenu();
		void UpdateDocuments();
		void CloseDocument(StableId);
		bool StopPlay();
		void AuthorComponentControls(SceneDocument&);
		void PrefabMenu();
		std::array<char, 64> m_ScriptAsset{}, m_BehaviorAsset{};
		std::array<char, 129> m_PropertyName{};
		int m_PropertyType = 2;
		PersistentEntityId m_OverrideRoot;
		std::vector<std::string> m_OverridePaths;
		std::optional<Tools::ProjectConfig> m_Project;
		std::filesystem::path m_RecoveryRoot;
		std::vector<std::filesystem::path> m_Recovery;
		std::map<StableId, std::filesystem::path> m_RestoredRecovery;
		std::chrono::steady_clock::time_point m_LastRecovery{};
		StableId m_Closing, m_PlayDocument;
		bool m_PlayPaused = false;
		struct PendingScene {
			SceneAsset scene;
			std::filesystem::path path, recovery;
			std::vector<AssetId> dependencies;
			size_t cursor = 0;
		};
		std::optional<PendingScene> m_PendingScene;
		struct PendingAuthor {
			std::filesystem::path path, recovery;
			std::string source, type;
			std::vector<AssetId> dependencies;
		};
		std::optional<PendingAuthor> m_PendingAuthor;
		SceneHandle m_HostScene;
		SceneHandle m_PreGroveScene;
		std::filesystem::path m_GrovePath;
		std::vector<SceneHandle> m_AuthorScenes;
		Application& m_App;
		DocumentManager m_Documents;
		EditorSelection m_Selection;
		PickingController m_Picking;
		TransformGizmo m_Gizmo;
		EditorCameraController m_Camera;
		bool m_RightLook = false;
		bool m_PlayUIActive = false, m_PlayUICapture = false;
		bool m_ResetCamera = true, m_ShowNormals = false, m_ShowGrid = false;
		AssetEditorRegistry m_AssetEditors;
		HierarchyPanel m_Hierarchy;
		InspectorPanel m_Inspector;
		StableId m_OpenAsset, m_LastScene;
		std::string m_Error;
		bool m_AskDiscard = false;
	};
}
