#pragma once
#include "Application/EditorSelection.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
	/* 枚举时只读场景，把增删和重挂接放在迭代结束后。 */
	class HierarchyPanel {
	public:
		/**
		 * @brief 绘制场景实体的层级树并处理选择交互。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in,out] selection 当前高亮的实体；点击、创建或删除实体时更新。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 层级操作改变了选择或场景时为 true；本帧仅展示层级时为 false。
		 */
		bool Draw(SceneDocument&, EditorSelection&, const PanelText&, std::string& error);

	private:
		std::array<char, 160> m_Search{};
	};
}
