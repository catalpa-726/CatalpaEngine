#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Assets/AssetManager.h"
#include "Documents/DocumentManager.h"

namespace Catalpa::Editor {
    // 在文档切换和关闭时收束旧编辑器的独立 surface，再激活新类型。
    class AssetEditorRegistry {
      public:
        using Factory = std::unique_ptr<IAssetEditor> (*)();
        using OpenDocument = AssetDocument* (*)(AssetId, const AssetManager&, DocumentManager&, std::string&);
        AssetEditorRegistry();
        bool Register(std::string_view type, Factory, OpenDocument, std::string&);
        AssetDocument* Open(AssetId, std::string_view type, const AssetManager&, DocumentManager&,
                            std::string&) const;
        void Draw(Application&, AssetDocument&, const PanelText&, std::string& error);
        void Close(Application&);

      private:
        struct Entry {
            Factory factory;
            OpenDocument open;
        };

        std::map<std::string, Entry, std::less<>> m_Entries;
        std::unique_ptr<IAssetEditor> m_Active;
        // 借用注册表内不会被替换的稳定键，不向文档借用字符串。
        std::string_view m_ActiveType;
    };
}
