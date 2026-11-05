#include "catpch.h"
#include "Log/LogSubscription.h"
#include "Log/Log.h"
#include <spdlog/sinks/sink.h>
#include <spdlog/formatter.h>

namespace Catalpa {
    namespace {
        struct Bus {
            std::mutex mutex;
            std::deque<LogRecord> records;
            std::atomic<uint64_t> dropped{0};
            uint64_t next = 1;
        };

        Bus& State() {
            static Bus bus;
            return bus;
        }

        class SubscriptionSink final : public spdlog::sinks::sink {
        public:
            void log(const spdlog::details::log_msg& message) override {
                auto& bus = State();
                std::unique_lock lock(bus.mutex, std::try_to_lock);
                if (!lock.owns_lock()) {
                    ++bus.dropped;
                    return;
                }
                try {
                    LogRecord record;
                    record.sequence = bus.next;
                    record.level = int(message.level);
                    record.time = message.time;
                    record.logger.assign(message.logger_name.data(),
                                         std::min<size_t>(message.logger_name.size(), 128));
                    record.message.assign(message.payload.data(),
                                          std::min<size_t>(message.payload.size(), 4096));
                    if (message.payload.size() > 4096)
                        record.message += " [truncated]";
                    bus.records.push_back(std::move(record));
                    ++bus.next;
                    if (bus.records.size() > 2048)
                        bus.records.pop_front();
                } catch (...) {
                    ++bus.dropped;
                }
            }

            void flush() override {}

            void set_pattern(const std::string&) override {} // 保存结构化记录，不重复格式化控制台颜色。

            void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
        };
    }

    void LogSubscription::AttachToLoggers() {
        static auto sink = std::make_shared<SubscriptionSink>();
        for (auto logger : {Log::GetCoreLogger(), Log::GetAppLogger()})
            if (logger) {
                auto& sinks = logger->sinks();
                if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end())
                    sinks.push_back(sink);
            }
    }

    LogSubscription::LogSubscription() : m_Next(1), m_ObservedDrops(0) {} // 重放仍在有界环内的启动日志。

    LogSubscription::~LogSubscription() = default;

    void LogSubscription::Clear() {
        auto& bus = State();
        std::lock_guard lock(bus.mutex);
        m_Next = bus.next;
        m_ObservedDrops = bus.dropped.load();
    }

    std::vector<LogRecord> LogSubscription::Drain(size_t maxRecords, uint64_t& dropped) {
        auto& bus = State();
        std::lock_guard lock(bus.mutex);
        const auto producerDrops = bus.dropped.load();
        // 成功入队的序号连续，可直接定位游标。复制全部完成后再推进，分配失败时可重试。
        const auto oldest = bus.records.empty() ? bus.next : bus.records.front().sequence;
        const auto first = std::max(m_Next, oldest);
        const size_t count = std::min(maxRecords, static_cast<size_t>(bus.next - first));
        const auto begin = bus.records.begin() + static_cast<std::ptrdiff_t>(first - oldest);
        std::vector<LogRecord> result(begin, begin + static_cast<std::ptrdiff_t>(count));
        dropped = (producerDrops - m_ObservedDrops) + (first - m_Next);
        m_ObservedDrops = producerDrops;
        m_Next = first + count;
        return result;
    }
}
