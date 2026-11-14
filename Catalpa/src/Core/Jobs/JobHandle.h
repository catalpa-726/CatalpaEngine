#pragma once
#include "catpch.h"

#include "Core/Jobs/CancellationToken.h"
namespace Catalpa {
	namespace JobDetail {
		struct State {
			CancellationSource cancellation;
			std::atomic_bool completed{false};
			std::string error;
		};
	}
	/* 仅表示 CPU 作业完成。即使调度器已回收作业，句柄仍保留取消状态和诊断信息。 */
	class JobHandle {
	public:
		[[nodiscard]] bool HasValue() const noexcept { return bool(m_State); }
		[[nodiscard]] bool IsComplete() const noexcept {
			return m_State && m_State->completed.load(std::memory_order_acquire);
		}
		void Cancel() const noexcept {
			if (m_State)
				m_State->cancellation.Cancel();
		}
		[[nodiscard]] std::string GetError() const { return IsComplete() ? m_State->error : std::string{}; }

	private:
		friend class JobSystem;
		std::shared_ptr<JobDetail::State> m_State;
	};
}
