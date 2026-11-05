#pragma once
#include "Documents/AssetDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa {
    class Application;
}

namespace Catalpa::Editor {
    // 类型编辑器只管理展示副本和离屏视图；撤销历史由资产文档拥有。
    class IAssetEditor {
      public:
        virtual ~IAssetEditor() = default;
        virtual bool Accepts(const AssetDocument&) const = 0;
        virtual void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) = 0;
        virtual void Close(Application&) = 0;
    };
}
