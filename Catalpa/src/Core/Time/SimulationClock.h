#pragma once

#include "Core/Base.h"
#include "Core/Time/FrameTime.h"

namespace Catalpa {

    enum class ClockError { None, InvalidArgument, InvalidOperation, Overflow };

    // Core 时间模块自己的诊断，不反向依赖 ECS 或 RHI 的错误类型。
    struct ClockStatus {
        ClockError code = ClockError::None;
        const char* message = "";
        [[nodiscard]] bool IsSuccess() const noexcept { return code == ClockError::None; }
    };

    struct SimulationClockConfig {
        double fixedDeltaSeconds = 1.0 / 60.0;
        double timeScale = 1.0;
        double maxFrameDeltaSeconds = 0.25;
        std::uint32_t maxTicksPerFrame = 8;
    };

    // 由创建线程使用的固定步时钟；调用方决定每个 tick 的实际系统执行。
    class CATALPA_API SimulationClock {
    public:
        [[nodiscard]] ClockStatus Configure(const SimulationClockConfig& config) noexcept;
        [[nodiscard]] ClockStatus Advance(double realSeconds, FrameTime& output) noexcept;
        void SetPaused(bool paused) noexcept;
        [[nodiscard]] ClockStatus RequestSingleStep() noexcept;
        [[nodiscard]] bool IsPaused() const noexcept { return m_Paused; }
        [[nodiscard]] const SimulationClockConfig& GetConfig() const noexcept { return m_Config; }
        // 显式重置时间/编号/积累，保留配置与暂停选择。
        void Reset() noexcept;

    private:
        SimulationClockConfig m_Config;
        double m_Accumulator = 0.0;
        double m_SimulationSeconds = 0.0;
        std::uint64_t m_TotalTicks = 0;
        bool m_Paused = false;
        bool m_StepRequested = false;
    };

}
