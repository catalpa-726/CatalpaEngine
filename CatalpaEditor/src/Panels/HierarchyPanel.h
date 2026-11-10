#pragma once
#include "Application/EditorSelection.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
    // 枚举时只读场景，把增删和重挂接放在迭代结束后。
    class HierarchyPanel {
      public:
        bool Draw(SceneDocument&, EditorSelection&, const PanelText&, std::string& error);
      private:
        std::array<char, 160> m_Search{};
    };
}
