#pragma once
#include "Panels/PanelText.h"
#include "Log/LogSubscription.h"

namespace Catalpa::Editor {
	/* 订阅有界值记录，生产者从不调用ImGui；暂停/隐藏不会扩大日志队列。 */
	class ConsolePanel final {
	public:
		/**
		 * @brief 从日志缓冲读取新消息，更新控制台列表。
		 */
		void Update();
		/**
		 * @brief 绘制日志列表及筛选、暂停和跟随控件。
		 *
		 * @param[in] visible 控制可见性的标志。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Draw(bool& visible, const PanelText&);

	private:
		LogSubscription m_Subscription;
		std::deque<LogRecord> m_Records;
		std::array<char, 128> m_Filter{};
		uint64_t m_Dropped = 0;
		bool m_Paused = false, m_Follow = true;
		int m_MinLevel = 0;
	};
}
