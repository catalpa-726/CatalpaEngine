#include "catpch.h"
#include "Core/Time/SimulationClock.h"

namespace Catalpa {

    ClockStatus SimulationClock::Configure(const SimulationClockConfig& config) noexcept
    {
        if (!std::isfinite(config.fixedDeltaSeconds) || config.fixedDeltaSeconds <= 0.0
            || !std::isfinite(config.timeScale) || config.timeScale < 0.0
            || !std::isfinite(config.maxFrameDeltaSeconds) || config.maxFrameDeltaSeconds <= 0.0
            || config.maxTicksPerFrame == 0)
            return { ClockError::InvalidArgument, "Clock configuration requires finite positive steps/limits and a nonnegative scale" };
        if (!std::isfinite(config.maxFrameDeltaSeconds * config.timeScale)
            || !std::isfinite(config.fixedDeltaSeconds * config.maxTicksPerFrame))
            return { ClockError::Overflow, "Clock configuration products exceed the finite seconds range" };
        m_Config = config;
        return {};
    }

    ClockStatus SimulationClock::Advance(double realSeconds, FrameTime& output) noexcept
    {
        if (!std::isfinite(realSeconds) || realSeconds < 0.0)
            return { ClockError::InvalidArgument, "Frame duration must be finite and nonnegative seconds" };
        if (m_TotalTicks == std::numeric_limits<std::uint64_t>::max())
            return { ClockError::Overflow, "Simulation tick identities are exhausted" };

        FrameTime frame;
        frame.realDeltaSeconds = realSeconds;
        frame.fixedDeltaSeconds = m_Config.fixedDeltaSeconds;
        frame.firstTick = m_TotalTicks + 1;
        double remaining = m_Accumulator;
        if (m_Paused) {
            frame.tickCount = m_StepRequested ? 1u : 0u;
            // 暂停展示当前权威状态；暂停期间的墙钟时间不进入 accumulator。
            frame.interpolationAlpha = 1.0;
        } else {
            const double acceptedReal = std::min(realSeconds, m_Config.maxFrameDeltaSeconds);
            const double scaledDelta = acceptedReal * m_Config.timeScale;
            const double cappedSeconds = (realSeconds - acceptedReal) * m_Config.timeScale;
            const double accumulated = m_Accumulator + scaledDelta;
            if (!std::isfinite(accumulated) || !std::isfinite(cappedSeconds))
                return { ClockError::Overflow, "Frame duration exceeds finite accumulated simulation time" };

            const double availableTicks = std::floor(accumulated / m_Config.fixedDeltaSeconds);
            if (!std::isfinite(availableTicks))
                return { ClockError::Overflow, "Fixed step is too small for this frame duration" };
            frame.tickCount = availableTicks >= m_Config.maxTicksPerFrame
                ? m_Config.maxTicksPerFrame : static_cast<std::uint32_t>(availableTicks);
            // 双精度乘减可能产生负的最后一个 ULP；不积累负时长。
            remaining = std::max(0.0, accumulated - frame.tickCount * m_Config.fixedDeltaSeconds);
            frame.droppedSeconds = cappedSeconds;
            if (remaining >= m_Config.fixedDeltaSeconds) {
                const double fractional = std::fmod(remaining, m_Config.fixedDeltaSeconds);
                frame.droppedSeconds += remaining - fractional;
                remaining = fractional;
            }
            if (!std::isfinite(frame.droppedSeconds))
                return { ClockError::Overflow, "Discarded simulation time exceeds finite seconds range" };
            frame.interpolationAlpha = std::min(remaining / m_Config.fixedDeltaSeconds, std::nextafter(1.0, 0.0));
        }

        if (m_TotalTicks > std::numeric_limits<std::uint64_t>::max() - frame.tickCount)
            return { ClockError::Overflow, "Simulation tick identities would wrap" };
        frame.simulationSeconds = m_SimulationSeconds + frame.tickCount * m_Config.fixedDeltaSeconds;
        if (!std::isfinite(frame.simulationSeconds))
            return { ClockError::Overflow, "Total simulation time exceeds finite seconds range" };
        if (frame.tickCount != 0 && frame.simulationSeconds <= m_SimulationSeconds)
            return { ClockError::Overflow, "Fixed step no longer advances the representable simulation time" };

        // 所有检查完成后同时发布状态与输出；错误不会吞掉待执行的单步。
        m_TotalTicks += frame.tickCount;
        m_SimulationSeconds = frame.simulationSeconds;
        m_Accumulator = remaining;
        m_StepRequested = false;
        output = frame;
        return {};
    }

    void SimulationClock::SetPaused(bool paused) noexcept
    {
        m_Paused = paused;
        if (!paused) m_StepRequested = false;
    }

    ClockStatus SimulationClock::RequestSingleStep() noexcept
    {
        if (!m_Paused) return { ClockError::InvalidOperation, "Single-step is only valid while paused" };
        if (m_StepRequested) return { ClockError::InvalidOperation, "A single simulation step is already pending" };
        m_StepRequested = true;
        return {};
    }

    void SimulationClock::Reset() noexcept
    {
        m_Accumulator = 0.0;
        m_SimulationSeconds = 0.0;
        m_TotalTicks = 0;
        m_StepRequested = false;
    }

}
