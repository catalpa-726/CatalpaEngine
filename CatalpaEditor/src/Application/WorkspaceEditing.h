#pragma once
#include "AssetEditors/AssetEditorRegistry.h"
#include "Documents/DocumentManager.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Viewport/PickingController.h"
#include "Viewport/TransformGizmo.h"

namespace Catalpa::Editor {
    class BuildPanel;

    // 工作区的文档和交互生命周期。宿主通过引用传入，SceneManager 与 GPU 资源均为借用。
    class WorkspaceEditing {
      public:
        explicit WorkspaceEditing(Application& app) : m_App(app) {}

        ~WorkspaceEditing();
        void Reset();
        void SceneTree(const PanelText&);
        bool Inspect(const PanelText&);
        void Draw(const PanelText&, BuildPanel*);
        void OpenAsset(AssetId, const BuildPanel&);
        void Menu(const PanelText&);
        bool HasDirtyDocuments() const;
        bool ShowViewport = true;

      private:
        SceneDocument* ActiveScene();
        void ActivateAsset(AssetDocument&);
        void DrawViewport(SceneDocument*, const PanelText&);
        Application& m_App;
        DocumentManager m_Documents;
        EditorSelection m_Selection;
        PickingController m_Picking;
        TransformGizmo m_Gizmo;
        AssetEditorRegistry m_AssetEditors;
        HierarchyPanel m_Hierarchy;
        InspectorPanel m_Inspector;
        StableId m_OpenAsset, m_LastScene;
        std::string m_Error;
        bool m_AskDiscard = false;
    };
}
