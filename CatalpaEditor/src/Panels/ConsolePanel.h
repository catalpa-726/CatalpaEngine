#pragma once
#include "Panels/PanelText.h"
#include "Log/LogSubscription.h"

namespace Catalpa::Editor {
    // 订阅有界值记录，生产者从不调用ImGui；暂停/隐藏不会扩大日志队列。
    class ConsolePanel final {
      public:
        void Update();
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
