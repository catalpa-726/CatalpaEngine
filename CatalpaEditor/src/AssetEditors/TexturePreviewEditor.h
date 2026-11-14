#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
	/* 通道与 mip 为预览设置，不更改 ImageAsset 像素或色彩空间解释。 */
	class TexturePreviewEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& d) const override { return d.Image() != nullptr; }

		/**
		 * @brief 从当前文档读取图像纹理数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
		void Close(Application&) override;

	private:
		StableId m_Document;
		int m_Mip = 0, m_Channels = 0;
	};
}
