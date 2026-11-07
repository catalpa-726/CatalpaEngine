#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Animation/AnimationAssetLoader.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Scene/Components/AnimationPreviewComponent.h"
#include "Graphs/GraphCanvas.h"

namespace Catalpa::Editor {
    class AssetEditorRegistry;
    // 草稿、历史和保存由 AssetDocument 拥有；编辑器只持有独立 Scene 和临时取样状态。
    class AnimationEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument&) const override;
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        bool Preview(Application&, AssetDocument&, const PanelText&, std::string& error);
        void BlendCanvas(AssetDocument&, BlendTreeAsset&, const PanelText&, std::string& error);
        SceneHandle m_Scene;
        PersistentEntityId m_Actor, m_Mesh, m_Camera;
        StableId m_Document, m_Selected;
        GraphCanvas m_Canvas;
        std::shared_ptr<const std::string> m_Source;
        std::shared_ptr<const std::string> m_CachedSource;
        AnimationPreviewComponent m_CachedPreview;
        AssetVersionRef<MeshSkinBinding> m_CachedBinding;
        AssetVersionRef<SkeletonAsset> m_SkeletonSource;
        ValidatedClipEvaluator m_ClipEvaluator;
        BlendTreeRuntime m_BlendPreview;
        AssetId m_DraftIdentity;
        uint64_t m_DraftRevision = 0;
        std::string m_CachedBindingText, m_CachedClipText;
        std::vector<char> m_Buffer;
        std::array<char, 48> m_Binding{}, m_Clip{};
        double m_Time = 0, m_LastClock = 0;
        bool m_Playing = false;
        bool m_Frame = true;
        std::map<std::string, float, std::less<>> m_Parameters;
    };
    bool RegisterAnimationEditors(AssetEditorRegistry&, std::string& error);
} // namespace Catalpa::Editor
