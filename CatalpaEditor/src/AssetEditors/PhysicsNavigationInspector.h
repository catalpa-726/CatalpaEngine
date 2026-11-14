#pragma once
#include "ECS/EntityId.h"
#include "Panels/PanelText.h"

#include <string>
namespace Catalpa::Editor {
	class SceneDocument;
	/**
	 * @brief 绘制物理导航属性面板。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 */
	void DrawPhysicsNavigationInspector(SceneDocument&, EntityId, std::string& error, const PanelText& text = {});
} /* 命名空间 Catalpa::Editor */
