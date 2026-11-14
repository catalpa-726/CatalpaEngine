#pragma once

#include "Core/Base.h"
#include "Core/Time/FrameTime.h"

namespace Catalpa {

	enum class ClockError { None, InvalidArgument, InvalidOperation, Overflow };

	/* Core 时间模块自己的诊断，不反向依赖 ECS 或 RHI 的错误类型。 */
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

	/* 由创建线程使用的固定步时钟；调用方决定每个 tick 的实际系统执行。 */
	class CATALPA_API SimulationClock {
	public:
		/**
		 * @brief 校验并应用固定步长、缩放和单帧追赶上限。
		 * 校验并应用固定步长和追赶上限等仿真时钟参数。
		 *
		 * @param[in] config 固定步长、单帧追赶次数等时钟配置。
		 *
		 * @return 新配置及其时间计算有效时返回成功，否则返回时钟错误。
		 */
		[[nodiscard]] ClockStatus Configure(const SimulationClockConfig& config) noexcept;
		/**
		 * @brief 将真实经过时间累积为固定步，输出本帧应推进的仿真信息。
		 *
		 * @param[in] realSeconds 真实经过的时间（秒）。
		 * @param[out] output 接收固定步数量及本次时钟推进信息。
		 *
		 * @return 时钟操作的状态，用于区分正常推进和输入或当前状态不满足要求。
		 */
		[[nodiscard]] ClockStatus Advance(double realSeconds, FrameTime& output) noexcept;
		/**
		 * @brief 设置模拟暂停状态。
		 *
		 * @param[in] paused 是否暂停。
		 */
		void SetPaused(bool paused) noexcept;
		/**
		 * @brief 请求在暂停状态下推进一个模拟步。
		 *
		 * @return 暂停且没有待执行单步时成功，否则返回 InvalidOperation。
		 */
		[[nodiscard]] ClockStatus RequestSingleStep() noexcept;
		[[nodiscard]] bool IsPaused() const noexcept { return m_Paused; }
		[[nodiscard]] const SimulationClockConfig& GetConfig() const noexcept { return m_Config; }
		/**
		 * @brief 重置时钟计数与累积时间，保留当前配置及暂停状态。
		 * 显式重置时间/编号/积累，保留配置与暂停选择。
		 */
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
