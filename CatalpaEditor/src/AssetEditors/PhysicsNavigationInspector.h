#pragma once
#include "ECS/EntityId.h"
#include "Panels/PanelText.h"

#include <string>
namespace Catalpa {
	class AssetManager;
}
namespace Catalpa::Editor {
	class SceneDocument;
	/**
	 * @brief 绘制物理导航属性面板。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[in] addType 在添加菜单中选定的组件名称，空串只显示现有组件。
	 * @param[in] reveal 本帧要在属性区定位的组件。
	 * @param[in] pick 选择形状、材质、地形或导航网格。
	 * @param[in] assets 获取选中资源的固定版本以支持撤销与重做。
	 */
	void DrawPhysicsNavigationInspector(SceneDocument&, EntityId, std::string& error, const PanelText& text,
	    std::string_view addType, const ComponentAssetPicker&, AssetManager&, InspectorSection reveal = {});
} /* 命名空间 Catalpa::Editor */
