#pragma once
#include "AssetEditors/AssetEditorRegistry.h"
#include "Documents/DocumentManager.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Viewport/PickingController.h"
#include "Viewport/TransformGizmo.h"
#include "Viewport/EditorCameraController.h"
#include "Scene/SceneAsset.h"

namespace Catalpa { class AssetDependencyGraph; }
namespace Catalpa::Editor {
    class BuildPanel;

    // 工作区的文档和交互生命周期。宿主通过引用传入，SceneManager 与 GPU 资源均为借用。
    class WorkspaceEditing {
      public:
        explicit WorkspaceEditing(Application& app);

        ~WorkspaceEditing();
        bool Reset(std::string& error);
        void SceneTree(const PanelText&);
        bool HasSceneSelection();
        void ClearSceneSelection();
        bool Inspect(const PanelText&);
        void Draw(const PanelText&, BuildPanel*, uint32_t centerDockId);
        void OpenAsset(AssetId, const BuildPanel&);
        void Menu(const PanelText&);
        void PlayToolbar(const PanelText&);
        bool PickViewport(SceneDocument&, const RenderSurfaceSnapshot&, uint32_t x, uint32_t y, std::string& error);
        Document* CurrentDocument() noexcept { return m_Documents.Active(); }
        const Document* CurrentDocument() const noexcept { return m_Documents.Active(); }
        bool HasDirtyDocuments() const;
        bool HasPendingWork() const noexcept {
            return m_PlayDocument.HasValue() || m_PendingScene.has_value() || m_PendingAuthor.has_value();
        }
        void SetProject(const Tools::ProjectConfig*);
        bool SaveAll();
        void DiscardRecoveryCopies();
        void OpenGroveShowcase();
        void ReturnFromGrove();
        bool CanReturnFromGrove() const noexcept { return m_PreGroveScene.HasValue(); }

      private:
        SceneDocument* ActiveScene();
        void ActivateAsset(AssetDocument&);
        void DrawViewport(SceneDocument*, const PanelText&, uint32_t centerDockId);
        EditorCameraInput ViewportInput(bool hovered, bool gizmoBusy);
        bool RoutePlayUIInput(float x, float y, float width, float height, Extent2D extent, bool hovered,
                              bool focused);
        void BlurPlayUI();
        bool SaveDocument(Document&, bool saveAs = false);
        bool LoadProjectCatalog();
        bool OpenAuthor(const std::filesystem::path&, std::string text = {}, std::string type = {},
                        const std::filesystem::path& recovery = {}, const AssetDependencyGraph* loadGraph = nullptr);
        bool InstallAuthorScene(const SceneAsset&, const std::filesystem::path&,
                                const std::filesystem::path& recovery = {});
        void RemoveRecovery(StableId);
        void RetainRecovery(const std::filesystem::path&);
        void CancelPendingOpen();
        void DocumentMenu();
        void UpdateDocuments();
        void CloseDocument(StableId);
        bool StopPlay();
        void AuthorComponentControls(SceneDocument&);
        void PrefabMenu();
        std::array<char,64> m_ScriptAsset{},m_BehaviorAsset{};
        std::array<char,129> m_PropertyName{};
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
