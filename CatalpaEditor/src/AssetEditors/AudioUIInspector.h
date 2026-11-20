#pragma once
#include "Documents/SceneDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa {
	class AssetManager;
}
namespace Catalpa::Editor {
	/**
	 * @brief 绘制音频和游戏界面组件的属性，并通过文档历史提交修改。
	 * 作者组件通过既有文档历史发布；编辑器不直接驱动设备 voice 或修改 PlayWorld。
	 *
	 * @param[in,out] assets 读取所选音频和界面资源的已加载版本。
	 * @param[in,out] doc 保存作者组件改动和撤销历史的场景文档。
	 * @param[in] entity 要显示与编辑音频或 UI 组件的场景实体。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] addType 在添加菜单中选定的组件名称，空串只显示现有组件。
	 * @param[in] reveal 本帧要在属性区定位的组件。
	 * @param[in] pick 选择项目中相应类型的作者资产。
	 */
	void DrawAudioUIInspector(AssetManager&, SceneDocument&, EntityId, const PanelText&, std::string& error,
	    std::string_view addType, const ComponentAssetPicker&, InspectorSection reveal = {});
} /* 命名空间 Catalpa::Editor */
