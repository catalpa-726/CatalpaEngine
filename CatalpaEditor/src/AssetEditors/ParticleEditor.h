#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Particles/ParticleEffect.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa::Editor {
    class AssetEditorRegistry;
    class ParticleEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& document) const override {
            return document.TypeKey() == "particle_effect";
        }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        bool Preview(Application&, AssetDocument&, ParticleEffect, std::string& error);
        SceneHandle m_Scene;
        PersistentEntityId m_Emitter;
        StableId m_Document, m_Effect;
        std::shared_ptr<const std::string> m_Published;
        uint64_t m_Revision = 0, m_Restart = 1;
        bool m_Emitting = true;
    };
    bool RegisterParticleEditor(AssetEditorRegistry&, std::string& error);
} // namespace Catalpa::Editor
