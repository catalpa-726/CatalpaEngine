#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Audio/AudioTypes.h"
namespace Catalpa::Editor {
    class AudioPreviewEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override {
            return doc.TypeKey() == "audio_clip";
        }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        StableId m_Document;
        AudioVoiceHandle m_Voice;
        float m_Gain = 1, m_Pitch = 1;
        bool m_Loop = false, m_Paused = false;
    };
} // namespace Catalpa::Editor
