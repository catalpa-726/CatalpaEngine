#pragma once

#include "catpch.h"

#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"

namespace Catalpa {

	class VulkanDevice;
	class VulkanFence;

	/*
	 * 单个 queue 的唯一提交/完成序列账本；当前仅使用覆盖整批提交的 Fence。
	 * Fence 和命令资源引用仍归帧所有，不建立第二个资源退休管理器。
	 */
	class VulkanSubmissionTracker {
	public:
		VulkanSubmissionTracker(VulkanDevice& device, QueueId queue);
		VulkanSubmissionTracker(const VulkanSubmissionTracker&) = delete;
		VulkanSubmissionTracker& operator=(const VulkanSubmissionTracker&) = delete;

		/**
		 * @brief 必须在 Fence reset/vkQueueSubmit2 之前调用；预留成功接受后的登记空间。
		 *
		 * @param[in] fence 拟用于本次提交的栅栏。
		 *
		 * @return 返回此次提交能否登记的状态；所有预留分配均发生在原生提交之前。
		 */
		[[nodiscard]] RHIStatus PrepareSubmission(VulkanFence& fence);

		/**
		 * @brief 只在 vkQueueSubmit2 成功且 Fence 标记 Pending 后调用；不分配内存。
		 *
		 * @param[in] fence 已由队列接收并处于待完成状态的栅栏。
		 */
		void AcceptSubmission(VulkanFence& fence) noexcept;

		/**
		 * @brief 仅接受已经由原生等待确认 Signaled 的 Fence，不以帧号推断完成。
		 *
		 * @param[in] fence 已经确认触发的提交栅栏。
		 */
		void NotifyFenceCompleted(VulkanFence& fence);
		/**
		 * @brief 先检查设备与队列身份，再从已确认完成的序号或栅栏查询指定批次。
		 *
		 * @param[in] point 本设备、本队列已经接受的序号，不能指向未来的提交。
		 * @param[out] complete 输出该完成点是否达到；调用开始先置为 false。
		 *
		 * @return 返回查询状态；调用成功时仍须检查 complete 是否为 true。
		 */
		[[nodiscard]] RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete);
		[[nodiscard]] RHICompletionPoint GetLastCompletionPoint() const noexcept;

		/**
		 * @brief 仅关闭/失设备时使用；解除 Fence 借用并永久停止本账本，不伪造完成。
		 */
		void DiscardPendingSubmissions() noexcept;

	private:
		[[nodiscard]] RHIStatus ValidateDevice() const;

		struct PendingSubmission {
			uint64_t sequence = 0;
			VulkanFence* fence = nullptr;
		};

		VulkanDevice& m_Device;
		const DeviceId m_DeviceId;
		const QueueId m_QueueId;
		uint64_t m_Submitted = 0;
		uint64_t m_Completed = 0;
		std::vector<PendingSubmission> m_Pending;
		bool m_Discarded = false;
	};

}
