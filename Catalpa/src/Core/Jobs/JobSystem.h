#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Core/Jobs/JobHandle.h"
namespace Catalpa {
    struct JobSystemConfig { uint32_t workerCount = 2; size_t maxPending = 64; };
    // One enkiTS scheduler; submitted jobs are pinned to workers, never executed by a UI Pump.
    // Start/Submit/Collect/Wait/Shutdown belong to the starting thread. Tokens may be cancelled anywhere.
    // Destroying a running scheduler on another thread is a fatal lifecycle contract violation.
    class CATALPA_API JobSystem {
    public:
        JobSystem();
        ~JobSystem();
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;
        bool Start(const JobSystemConfig&, std::string& error);
        bool Submit(std::function<void(const CancellationToken&)> work, JobHandle& output, std::string& error);
        void CollectCompleted();
        // Owner-thread check includes enkiTS completion, so task storage can no longer be in use.
        [[nodiscard]] bool IsComplete(const JobHandle&) const noexcept;
        bool Wait(const JobHandle&, std::string& error);
        void Shutdown() noexcept;
        [[nodiscard]] size_t GetPendingCount() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
