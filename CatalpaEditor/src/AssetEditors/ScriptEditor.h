#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
    // 文本草稿可暂含语法错误；Validate/Cook 拒绝错误版本，运行中的旧版本继续有效。
    class ScriptEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override { return doc.Source() && (doc.TypeKey()=="script" || doc.TypeKey()=="prefab" || doc.TypeKey()=="material_definition"); }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
        void Close(Application&) override { m_Source.reset(); m_Buffer.clear(); m_Group=0; }
      private:
        std::shared_ptr<const std::string> m_Source;
        std::vector<char> m_Buffer;
        uint64_t m_Group = 0;
    };
}
