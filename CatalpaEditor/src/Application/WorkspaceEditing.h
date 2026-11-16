#pragma once
#include "AssetEditors/AssetEditorRegistry.h"
#include "Documents/DocumentManager.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/InspectorPanel.h"
#include "Viewport/PickingController.h"
#include "Viewport/TransformGizmo.h"
#include "Viewport/EditorCameraController.h"
#include "Scene/SceneAsset.h"
#include "Scene/SceneSubtree.h"
#include "Application/WorkspaceAssetActions.h"
#include "Application/WorkspaceSettings.h"

namespace Catalpa {
	class AssetDependencyGraph;
}
namespace Catalpa::Editor {
	class BuildPanel;

	/* 工作区的文档和交互生命周期。宿主通过引用传入，SceneManager 与 GPU 资源均为借用。 */
	class WorkspaceEditing {
	public:
		explicit WorkspaceEditing(Application& app);

		/** @brief 供工作区恢复及保存独立于场景文档的导航和吸附偏好。 */
		ViewportSettings& ViewportPreferences() noexcept { return m_ViewportSettings; }

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
		/** @brief 在所有作者窗口绘制后处理未被文本输入和运行模式占用的快捷键。 */
		void HandleShortcuts();
		bool TakeHierarchyRequest() noexcept { return std::exchange(m_RevealHierarchy, false); }
		/**
		 * @brief 处理本项目的异步源资产动作并提交到发起编辑的文档。
		 * @param[in,out] build 当前构建服务，空值表示尚未绑定。
		 * @param[in] shaderRoot 构建资产使用的着色器目录。
		 */
		void UpdateAssetActions(BuildPanel* build, const std::filesystem::path& shaderRoot);
		/**
		 * @brief 打开作者源文件，为脚本和图文档绑定实际构建身份。
		 * @param[in] path 要打开的项目源文件绝对路径。
		 */
		void OpenSourceAsset(const std::filesystem::path& path);
		/**
		 * @brief 提交模型或预制体资产在场景视口内的放置请求。
		 * @param[in] path 项目源文件路径。
		 * @param[in] position 放置点的世界坐标，单位为米。
		 */
		void DropSourceIntoScene(const std::filesystem::path& path, Math::Vec3 position);
		/**
		 * @brief 记录外部文件拖入视口时的落点与场景代次。
		 * @param[in] x 光标客户区水平坐标，单位为窗口逻辑像素。
		 * @param[in] y 光标客户区垂直坐标，单位为窗口逻辑像素。
		 * @return 导入成功后调用的放置函数，视口以外返回空函数。
		 */
		ImportPlacement MakeImportPlacement(float x, float y);
		/**
		 * @brief 按作者类型创建未保存的文档并打开对应编辑器。
		 * @param[in] type 受支持的脚本、行为图或材质图类型。
		 */
		void CreateAssetDraft(std::string_view type);
		/** @brief 保存当前活动文档，供发布面板与快捷键复用同一保存路径。 */
		bool SaveActive() {
			auto* doc = m_Documents.Active();
			return doc && SaveDocument(*doc);
		}
		SceneDocument* CurrentScene() { return ActiveScene(); }
		/**
		 * @brief 保存当前作者场景；未命名场景即使尚无编辑记录也使用另存为。
		 * @return 作者场景写入成功时为 true；取消、Play 或写入失败时为 false。
		 */
		bool SaveCurrentScene() {
			auto* scene = ActiveScene();
			return scene && SaveDocument(*scene);
		}

	private:
		/**
		 * @brief 检查实例是否使用作者或 Cook 后的标准单色无光照定义。
		 * @param[in] source 当前打开的材质实例。
		 * @return 定义明确由引擎提供，且允许按基础色复制时为 true。
		 */
		static bool IsStandardUnlit(const MaterialInstance& source);
		/**
		 * @brief 打开独立的标准 PBR 草稿，会话和作者项目均可预览。
		 * @param[in] source 提供基础色和面剔除的标准无光照材质；空指针表示沿用标准 PBR 默认值。
		 */
		void CreatePbrDraft(const MaterialInstance* source = nullptr);
		SceneDocument* ActiveScene();
		void ActivateAsset(AssetDocument&);
		void DrawViewport(SceneDocument*, const PanelText&, uint32_t centerDockId);
		EditorCameraInput ViewportInput(bool hovered, bool gizmoBusy, bool canAdjustSpeed);
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
		enum class EditAction { Undo, Redo, Copy, Paste, Delete, Rename, Frame, Save };
		/**
		 * @brief 检查活动文档、运行状态和当前选择是否允许执行编辑动作。
		 * @param[in] action 菜单、快捷键或上下文入口请求的动作。
		 * @return 该动作当前可提交时为 true。
		 */
		bool CanEditAction(EditAction);
		/**
		 * @brief 在统一的文档历史入口执行编辑动作，失败原因保留在工作区诊断中。
		 * @param[in] action 需要执行的编辑动作，执行前重新检查可用状态。
		 */
		void PerformEditAction(EditAction);
		/** @brief 按所选子树的世界包围盒和当前视口宽高比调整独立编辑相机。 */
		void FrameSelection();
		/**
		 * @brief 绘制实体的脚本或行为图选择器并接收兼容的拖放。
		 * @param[in,out] doc 所选实体所属的作者场景。
		 * @param[in] id 目标实体的持久身份。
		 * @param[in] script 为 true 时仅筛选 Lua 源文件。
		 * @param[in] bound 当前引用的稳定资产 ID。
		 */
		void DrawBehaviorPicker(SceneDocument& doc, PersistentEntityId id, bool script, AssetId bound);
		/** @brief 在网格的指定材质槽选择项目材质，异步构建期间固定实体、网格与原材质。 */
		void DrawMaterialPicker(
		    SceneDocument& doc, PersistentEntityId id, size_t slot, const MeshRendererComponent& mesh);
		/** @brief 优先打开绑定材质的项目源文件；内置或导入子材质使用独立草稿。 */
		void OpenBoundMaterial(const AssetVersionRef<MaterialInstance>& material);
		/**
		 * @brief 提交构建请求并固定后续实例化或组件编辑的目标。
		 * @param[in] source 需要构建的源文件。
		 * @param[in] action 构建成功后提交的作者操作。
		 * @param[in,out] doc 当前可编辑的场景文档。
		 * @param[in] entity 需要绑定行为或材质的目标身份。
		 * @param[in] position 放置实体时的世界位置，单位为米。
		 * @return 成功排队并记录目标时为 true。
		 */
		bool StartSourceAction(const std::filesystem::path& source, AuthorAssetAction action,
		    SceneDocument* doc = nullptr, PersistentEntityId entity = {}, Math::Vec3 position = {});
		WorkspaceAssetActions m_AssetActions;
		Math::Vec4 m_DropRect{};
		StableId m_DropDocument;
		uint64_t m_DropEpoch = 0;
		std::optional<SceneSubtreeSnapshot> m_Clipboard;
		StableId m_ClipboardDocument;
		bool m_RevealHierarchy = false, m_GameView = false;
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
		ViewportSettings m_ViewportSettings;
		bool m_RightLook = false;
		/* 中键手势只延续到连续命中场景图像的下一帧，切换视图或失焦后须重新按下。 */
		int m_SpeedGestureFrame = -2;
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
