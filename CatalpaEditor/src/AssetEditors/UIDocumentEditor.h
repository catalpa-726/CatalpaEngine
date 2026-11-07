#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "UI/UIRuntime.h"
#include "Scene/SceneHandle.h"
namespace Catalpa::Editor {
    class UIDocumentEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override {
            return doc.TypeKey() == "ui_document";
        }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        bool BuildPreview(Application&, AssetDocument&, std::string& error);
        std::shared_ptr<UIRuntime> m_Runtime;
        std::vector<char> m_Text;
        std::shared_ptr<const std::string> m_Bound, m_TextSource;
        SceneHandle m_Scene;
        StableId m_Document;
        uint64_t m_Version = 1;
        double m_Elapsed = 0;
        bool m_PointerCaptured = false;
    };
    class AssetEditorRegistry;
    bool RegisterAudioUIEditors(AssetEditorRegistry&, std::string& error);
} // namespace Catalpa::Editor
