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
		 * @param[in] picker 网格组件各材质槽的可选作者资源选择器。
		 *
		 * @return 选中实体及其身份组件有效且绘制了属性时为 true。
		 */
		bool Draw(SceneDocument&, const EditorSelection&, const PanelText&, MaterialSlotEdit&,
		    std::string& error, const MaterialSlotPicker& picker = {});
		/**
		 * @brief 取走添加组件菜单发起的脚本或行为图选择请求。
		 * @param[in] script 为真时查询 C# 脚本，否则查询行为图。
		 * @return 有对应的未消费请求时返回真。
		 */
		bool TakeBehaviorPicker(bool script);
		InspectorSection RevealedSection() const { return m_Revealed; }
		/** @brief 无需选择资源的组件只派发一次，避免失败或撤销后自动再次挂载。 */
		std::string ComponentRequest() {
			if (m_PendingType == "RigidBodyComponent" || m_PendingType == "CharacterControllerComponent" ||
			    m_PendingType == "ColliderComponent" || m_PendingType == "AudioListenerComponent")
				return std::exchange(m_PendingType, {});
			return m_PendingType;
		}

	private:
		void DrawName(SceneDocument&, EntityReference, const IdentityComponent&, const PanelText&, std::string&);
		/**
		 * @brief 显示实际组件和受检的添加入口；资源选择保留至提交或切换实体。
		 * @param[in,out] doc 保存作者命令和撤销记录的场景文档。
		 * @param[in] ref 选中实体的持久身份及有效代次。
		 * @param[in] text 提供当前语言的组件和操作名称。
		 * @param[out] error 组件读取或编辑失败的原因。
		 */
		void DrawComponents(SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error);
		EntityReference m_ComponentTarget;
		InspectorSection m_Revealed, m_NextReveal;
		std::string m_PendingType;
		std::array<char, 160> m_ComponentFilter{};
		bool m_ScriptPicker = false, m_GraphPicker = false;
		std::vector<char> m_NameBuffer;
		EntityReference m_NameTarget;
		std::string m_NameAtActivation;
		PropertyEditorRegistry m_Properties;
	};
}
