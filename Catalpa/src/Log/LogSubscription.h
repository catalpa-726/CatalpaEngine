#pragma once
#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    struct LogRecord {
        uint64_t sequence = 0;
        int level = 0; // 沿用spdlog trace..critical数值，公开值不持有spdlog/UI对象。
        std::chrono::system_clock::time_point time;
        std::string logger, message;
    };

    class CATALPA_API LogSubscription final {
    public:
        LogSubscription(); // 首次Drain重放有界历史；Clear从当前时刻重新开始。
        ~LogSubscription();
        LogSubscription(const LogSubscription&) = delete;
        LogSubscription& operator=(const LogSubscription&) = delete;
        // 从本订阅游标取至多maxRecords条；计入覆盖与生产线程竞争丢弃数。
        std::vector<LogRecord> Drain(size_t maxRecords, uint64_t& dropped);
        void Clear();
        static void AttachToLoggers(); // Log::Init后安装一次共享有界sink。
    private:
        uint64_t m_Next = 0, m_ObservedDrops = 0;
    };
}
