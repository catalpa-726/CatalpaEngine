#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Scene/Components/MeshRendererComponent.h"

namespace Catalpa::Editor {
    class MaterialEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& d) const override {
            return bool(d.Material());
        }

        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        bool UpdateScene(Application&, AssetDocument&, std::string&);
        StableId m_Document, m_MaterialKey;
        SceneHandle m_Scene;
        PersistentEntityId m_Object;
        MeshRendererComponent m_Renderer;
        std::shared_ptr<const MaterialInstance> m_Bound;
        uint64_t m_Version = 0;
    };
}
