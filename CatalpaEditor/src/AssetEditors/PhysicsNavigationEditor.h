#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
    // 领域作者设置与自有值预览；撤销/保存仍走AssetDocument。
    class PhysicsNavigationEditor final : public IAssetEditor {
      public:
        PhysicsNavigationEditor();
        ~PhysicsNavigationEditor();
        bool Accepts(const AssetDocument&) const override;
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
        void Close(Application&) override;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa::Editor
