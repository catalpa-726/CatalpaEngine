#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Scene/Components/MeshRendererComponent.h"

namespace Catalpa::Editor {
	class MaterialEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& d) const override { return bool(d.Material()); }

		/**
		 * @brief 从当前文档读取材质实例数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 绘制预览。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawPreview(Application&, AssetDocument&, std::string& error);
		/**
		 * @brief 结束材质实例文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭材质预览表面并卸载临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		bool UpdateScene(Application&, AssetDocument&, std::string&);
		StableId m_Document, m_MaterialKey;
		SceneHandle m_Scene;
		PersistentEntityId m_Object;
		MeshRendererComponent m_Renderer;
		std::shared_ptr<const MaterialInstance> m_Bound;
		uint64_t m_Version = 0;
		bool m_Sphere = true;
	};
}
