#include "catpch.h"
#include "Core/Jobs/JobSystem.h"
#include "TaskScheduler.h"
#include "Log/Log.h"
namespace Catalpa {
    struct JobSystem::Impl {
        struct Task final : enki::IPinnedTask {
            std::shared_ptr<JobDetail::State> state;
            std::function<void(const CancellationToken&)> work;
            void Execute() override {
                try { work(state->cancellation.Token()); }
                catch (const std::exception& exception) { try { state->error = exception.what(); } catch (...) {} }
                catch (...) { try { state->error = "Unhandled worker exception"; } catch (...) {} }
                state->completed.store(true, std::memory_order_release);
            }
        };
        enki::TaskScheduler scheduler;
        std::vector<std::unique_ptr<Task>> tasks;
        JobSystemConfig config;
        std::thread::id owner;
        uint32_t nextWorker = 0;
        bool running = false;
    };
    JobSystem::JobSystem() : m_Impl(std::make_unique<Impl>()) {}
    JobSystem::~JobSystem() { Shutdown(); }
    bool JobSystem::Start(const JobSystemConfig& config, std::string& error) {
        try {
            if (m_Impl->running || config.workerCount == 0 || config.workerCount > 64 || config.maxPending == 0 || config.maxPending > 4096) {
                error = "Jobs require 1..64 explicit workers, bounded capacity and a stopped scheduler"; return false;
            }
            m_Impl->tasks.reserve(config.maxPending);
            m_Impl->config = config; m_Impl->owner = std::this_thread::get_id();
            m_Impl->scheduler.Initialize(config.workerCount + 1);
            m_Impl->running = true; error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool JobSystem::Submit(std::function<void(const CancellationToken&)> work, JobHandle& output, std::string& error) {
        try {
            if (!m_Impl->running || m_Impl->owner != std::this_thread::get_id() || !work) { error = "Job submission requires the running scheduler's owner thread"; return false; }
            CollectCompleted();
            if (m_Impl->tasks.size() >= m_Impl->config.maxPending) { error = "Job queue capacity is exhausted"; return false; }
            auto task = std::make_unique<Impl::Task>();
            task->state = std::make_shared<JobDetail::State>(); task->work = std::move(work);
            task->threadNum = 1 + (m_Impl->nextWorker++ % m_Impl->config.workerCount);
            JobHandle handle; handle.m_State = task->state;
            auto* scheduled = task.get();
            m_Impl->tasks.push_back(std::move(task));
            m_Impl->scheduler.AddPinnedTask(scheduled);
            output = std::move(handle); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    void JobSystem::CollectCompleted() {
        if (!m_Impl->running || m_Impl->owner != std::this_thread::get_id()) return;
        std::erase_if(m_Impl->tasks, [](const auto& task) { return task->GetIsComplete(); });
    }
    bool JobSystem::IsComplete(const JobHandle& handle) const noexcept {
        if (m_Impl->owner != std::this_thread::get_id() || !handle.IsComplete()) return false;
        for (const auto& task : m_Impl->tasks) if (task->state == handle.m_State) return task->GetIsComplete();
        return true;
    }
    bool JobSystem::Wait(const JobHandle& handle, std::string& error) {
        if (!handle.HasValue() || m_Impl->owner != std::this_thread::get_id()) { error = "Wait requires a valid job and its scheduler owner"; return false; }
        for (const auto& task : m_Impl->tasks) if (task->state == handle.m_State) {
            m_Impl->scheduler.WaitforTask(task.get()); error = handle.GetError(); return error.empty();
        }
        if (handle.IsComplete()) { error = handle.GetError(); return error.empty(); }
        error = "Job belongs to another scheduler"; return false;
    }
    void JobSystem::Shutdown() noexcept {
        if (!m_Impl || !m_Impl->running) return;
        if (m_Impl->owner != std::this_thread::get_id()) {
            if (Log::GetCoreLogger()) CAT_CORE_CRITICAL("JobSystem shutdown must run on its owner thread");
            std::abort();
        }
        for (const auto& task : m_Impl->tasks) task->state->cancellation.Cancel();
        m_Impl->scheduler.WaitforAllAndShutdown();
        m_Impl->tasks.clear(); m_Impl->running = false;
    }
    size_t JobSystem::GetPendingCount() const noexcept { return m_Impl->tasks.size(); }
    bool JobSystem::IsRunning() const noexcept { return m_Impl->running; }
}
