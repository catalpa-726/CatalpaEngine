#pragma once
#include "Panels/PanelText.h"
namespace Catalpa {
	class Application;
}
namespace Catalpa::Editor {
	class AudioMixerPanel final {
	public:
		/**
		 * @brief 绘制音频总线、增益与运行状态控件。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Draw(Application&, const PanelText&, std::string& error);

	private:
		float m_Master = 1;
	};
} /* 命名空间 Catalpa::Editor */
