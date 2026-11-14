#pragma once
#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	struct LogRecord {
		uint64_t sequence = 0;
		int level = 0; /* 沿用spdlog trace..critical数值，公开值不持有spdlog/UI对象。 */
		std::chrono::system_clock::time_point time;
		std::string logger, message;
	};

	class CATALPA_API LogSubscription final {
	public:
		LogSubscription(); /* 首次Drain重放有界历史；Clear从当前时刻重新开始。 */
		~LogSubscription();
		LogSubscription(const LogSubscription&) = delete;
		LogSubscription& operator=(const LogSubscription&) = delete;
		/**
		 * @brief 从订阅游标批量读取日志，分别计入覆盖和生产者丢弃。
		 * 从本订阅游标取至多maxRecords条；计入覆盖与生产线程竞争丢弃数。
		 *
		 * @param[in] maxRecords 本次最多从该订阅读取的日志条数。
		 * @param[out] dropped 接收订阅游标落后造成的覆盖数及生产端自上次读取以来的丢弃数之和。
		 *
		 * @return 按序读取的日志记录，数量不超过 maxRecords。
		 */
		std::vector<LogRecord> Drain(size_t maxRecords, uint64_t& dropped);
		/**
		 * @brief 清除当前订阅积压的日志记录。
		 */
		void Clear();
		/**
		 * @brief 挂接目标日志记录器。
		 */
		static void AttachToLoggers(); /* Log::Init后安装一次共享有界sink。 */
	private:
		uint64_t m_Next = 0, m_ObservedDrops = 0;
	};
}
