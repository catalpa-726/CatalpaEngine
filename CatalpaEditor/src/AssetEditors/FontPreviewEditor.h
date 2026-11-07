#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "UI/UIRuntime.h"
#include "UI/FontProfile.h"
#include "Scene/SceneHandle.h"
namespace Catalpa::Editor {
    class FontPreviewEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override {
            return doc.TypeKey() == "font";
        }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        StableId m_Document;
        SceneHandle m_Scene;
        std::unique_ptr<UIRuntime> m_Runtime;
        FontProfile m_Profile;
        double m_Elapsed = 0;
        uint64_t m_FontVersion = 0;
    };
    class AssetEditorRegistry;
    bool RegisterFontPreviewEditor(AssetEditorRegistry&, std::string& error);
} // namespace Catalpa::Editor
