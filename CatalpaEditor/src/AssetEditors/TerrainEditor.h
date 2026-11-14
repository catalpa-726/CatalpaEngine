#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa::Editor {
	class TerrainEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument&) const override;
		/**
		 * @brief 从当前文档读取地形数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
		/**
		 * @brief 结束地形文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭地形预览表面及临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		SceneHandle m_Scene;
		StableId m_Document;
		PersistentEntityId m_Object, m_Camera;
		uint64_t m_Version = 0;
	};
} /* 命名空间 Catalpa::Editor */
