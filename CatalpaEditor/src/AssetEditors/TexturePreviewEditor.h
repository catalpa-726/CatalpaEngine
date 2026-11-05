#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
    // 通道与 mip 为预览设置，不更改 ImageAsset 像素或色彩空间解释。
    class TexturePreviewEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& d) const override {
            return d.Image() != nullptr;
        }

        void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
        void Close(Application&) override;

      private:
        StableId m_Document;
        int m_Mip = 0, m_Channels = 0;
    };
}
