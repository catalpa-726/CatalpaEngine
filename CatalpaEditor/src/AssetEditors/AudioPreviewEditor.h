#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Audio/AudioTypes.h"
namespace Catalpa::Editor {
	class AudioPreviewEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override { return doc.TypeKey() == "audio_clip"; }
		/**
		 * @brief 从当前文档读取音频片段数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 结束音频片段文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 */
		void Close(Application&) override;

	private:
		StableId m_Document;
		AudioVoiceHandle m_Voice;
		float m_Gain = 1, m_Pitch = 1;
		bool m_Loop = false, m_Paused = false;
	};
} /* 命名空间 Catalpa::Editor */
