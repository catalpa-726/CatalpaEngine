#pragma once
#include "Application/EditorSelection.h"
#include "Properties/PropertyEditorRegistry.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
	class InspectorPanel {
	public:
		/**
		 * @brief 根据当前选择绘制主窗口或编辑对象的属性。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] selection 当前场景内要显示属性的实体选择。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] openMaterial 用户请求编辑材质时接收所选材质实例的固定版本引用。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 选中实体及其身份组件有效且绘制了属性时为 true。
		 */
		bool Draw(SceneDocument&, const EditorSelection&, const PanelText&, AssetVersionRef<MaterialInstance>&,
		    std::string& error);

	private:
		void DrawName(SceneDocument&, EntityReference, const IdentityComponent&, const PanelText&, std::string&);
		std::vector<char> m_NameBuffer;
		EntityReference m_NameTarget;
		std::string m_NameAtActivation;
		PropertyEditorRegistry m_Properties;
	};
}
