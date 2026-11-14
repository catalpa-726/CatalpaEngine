#pragma once
#include "Panels/PanelText.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Assets/AssetDependencyGraph.h"

namespace Catalpa::Editor {
	/* 按当前选中的 Cooked 资产显示直接依赖及受其变化影响的闭包。 */
	class DependencyPanel final {
	public:
		/**
		 * @brief 展示选中资源的依赖信息。
		 *
		 * @param[in] visible 控制可见性的标志。
		 * @param[in] result 本次操作使用的 Tools::AssetBuildResult。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Draw(bool& visible, const Tools::AssetBuildResult&, const PanelText&);

	private:
		void Select(AssetId id);
		std::string m_Key, m_Error;
		AssetDependencyGraph m_Graph;
		AssetId m_Selected;
		std::vector<AssetId> m_Direct, m_Affected;
	};
}
