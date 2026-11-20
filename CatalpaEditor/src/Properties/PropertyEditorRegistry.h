#pragma once
#include "Documents/SceneDocument.h"
#include "Materials/MaterialInstance.h"
#include "Geometry/MeshAsset.h"
#include "Panels/PanelText.h"
#include "Properties/RotationEditorState.h"

namespace Catalpa::Editor {
	/* 材质槽的资源选择由工作区提供，属性表负责布局和打开编辑器。 */
	struct MaterialSlotEdit {
		size_t slot = 0;
		AssetVersionRef<MaterialInstance> material;
		AssetVersionRef<MeshAsset> mesh;
	};
	using MaterialSlotPicker = std::function<void(size_t, const MeshRendererComponent&)>;
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
		 * @param[in] picker 可选的材质选择或拖放控件，按网格槽位绘制。
		 * @param[in] reveal 本帧请求展开并定位的组件。
		 */
		void Draw(SceneDocument&, EntityReference, const PanelText&, MaterialSlotEdit&, std::string&,
		    const MaterialSlotPicker& picker = {}, InspectorSection reveal = {}) const;

	private:
		bool DrawTransform(SceneDocument&, EntityReference, const PanelText&, std::string& error, InspectorSection reveal) const;
		mutable RotationEditorState m_Rotation;
	};
}
