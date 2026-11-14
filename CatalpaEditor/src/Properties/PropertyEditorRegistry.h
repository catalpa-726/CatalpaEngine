#pragma once
#include "Documents/SceneDocument.h"
#include "Materials/MaterialInstance.h"
#include "Panels/PanelText.h"
#include "Properties/RotationEditorState.h"

namespace Catalpa::Editor {
	/* 根据模式中的稳定字段标识选择值控件，并在提交时复用场景对作者数据的校验。 */
	class PropertyEditorRegistry {
	public:
		/**
		 * @brief 将所选实体的属性编辑分派给匹配的组件控件。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] ref 本次操作使用的 EntityReference。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[in] openMaterial 打开材质。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Draw(
		    SceneDocument&, EntityReference, const PanelText&, AssetVersionRef<MaterialInstance>&, std::string&) const;

	private:
		bool DrawTransform(SceneDocument&, EntityReference, const PanelText&, std::string& error) const;
		mutable RotationEditorState m_Rotation;
	};
}
