#pragma once
#include "Panels/PanelText.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Assets/AssetDependencyGraph.h"

namespace Catalpa::Editor {
    // 按当前选中的 Cooked 资产显示直接依赖及受其变化影响的闭包。
    class DependencyPanel final {
      public:
        void Draw(bool& visible, const Tools::AssetBuildResult&, const PanelText&);

      private:
        void Select(AssetId id);
        std::string m_Key, m_Error;
        AssetDependencyGraph m_Graph;
        AssetId m_Selected;
        std::vector<AssetId> m_Direct, m_Affected;
    };
}
