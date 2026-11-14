#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "UI/UIRuntime.h"
#include "UI/FontProfile.h"
#include "Scene/SceneHandle.h"
namespace Catalpa::Editor {
	class FontPreviewEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override { return doc.TypeKey() == "font"; }
		/**
		 * @brief 从当前文档读取字体数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 结束字体文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭字体预览表面并卸载临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		StableId m_Document;
		SceneHandle m_Scene;
		std::unique_ptr<UIRuntime> m_Runtime;
		FontProfile m_Profile;
		double m_Elapsed = 0;
		uint64_t m_FontVersion = 0;
	};
	class AssetEditorRegistry;
	bool RegisterFontPreviewEditor(AssetEditorRegistry&, std::string& error);
} /* 命名空间 Catalpa::Editor */
