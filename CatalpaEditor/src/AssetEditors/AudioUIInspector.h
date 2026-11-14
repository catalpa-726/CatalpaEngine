#pragma once
#include "Documents/SceneDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa {
	class Application;
}
namespace Catalpa::Editor {
	/**
	 * @brief 绘制音频和游戏界面组件的属性，并通过文档历史提交修改。
	 * 作者组件通过既有文档历史发布；编辑器不直接驱动设备 voice 或修改 PlayWorld。
	 *
	 * @param[in,out] app 提供组件编辑所需资产与运行时服务的宿主。
	 * @param[in,out] doc 保存作者组件改动和撤销历史的场景文档。
	 * @param[in] entity 要显示与编辑音频或 UI 组件的场景实体。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 */
	void DrawAudioUIInspector(Application&, SceneDocument&, EntityId, const PanelText&, std::string& error);
} /* 命名空间 Catalpa::Editor */
