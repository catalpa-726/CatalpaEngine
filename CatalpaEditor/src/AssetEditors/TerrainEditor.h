#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa::Editor {
    class TerrainEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument&) const override;
        void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
        void Close(Application&) override;

      private:
        SceneHandle m_Scene;
        StableId m_Document;
        PersistentEntityId m_Object, m_Camera;
        uint64_t m_Version = 0;
    };
} // namespace Catalpa::Editor
