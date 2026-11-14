#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "UI/UIRuntime.h"
#include "Scene/SceneHandle.h"
namespace Catalpa::Editor {
	class UIDocumentEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override { return doc.TypeKey() == "ui_document"; }
		/**
		 * @brief 从当前文档读取游戏 UI 文档数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 关闭游戏界面文档，并释放输入法目标和临时预览场景。
		 * 结束游戏 UI 文档文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于解除输入法目标、关闭 UI 预览及临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		bool BuildPreview(Application&, AssetDocument&, std::string& error);
		std::shared_ptr<UIRuntime> m_Runtime;
		std::vector<char> m_Text;
		std::shared_ptr<const std::string> m_Bound, m_TextSource;
		SceneHandle m_Scene;
		StableId m_Document;
		uint64_t m_Version = 1;
		double m_Elapsed = 0;
		bool m_PointerCaptured = false;
	};
	class AssetEditorRegistry;
	/**
	 * @brief 注册音频界面编辑器。
	 *
	 * @param[in] registry 接收音频与 UI 文档编辑器工厂的注册表。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool RegisterAudioUIEditors(AssetEditorRegistry&, std::string& error);
} /* 命名空间 Catalpa::Editor */
