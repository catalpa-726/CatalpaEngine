#pragma once
#include "Documents/SceneDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
    // 通过 schema 的稳定字段 ID 选择值控件，提交时复用 Scene 的作者校验。
    class PropertyEditorRegistry {
      public:
        void Draw(SceneDocument&, EntityReference, const PanelText&, std::string&) const;

      private:
        bool DrawTransform(SceneDocument&, EntityReference, std::string& error) const;
    };
}
