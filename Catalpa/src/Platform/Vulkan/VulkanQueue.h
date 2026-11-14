#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIQueue.h"
#include "VulkanSubmissionTracker.h"

/* 包装已有原生队列，提供提交入口，保存队列发生的终止性失败 */
namespace Catalpa {

	class VulkanDevice;
	class VulkanCommandList;
	class VulkanFence;

	class VulkanQueue : public RHIQueue {
	public:
		VulkanQueue(VulkanDevice& device, VkQueue queue, uint32_t queueFamily, QueueId id)
		    : m_Device(device), m_Queue(queue), m_QueueFamily(queueFamily), m_Id(id), m_Submissions(device, id) {}
		~VulkanQueue() override;
		/**
		 * @brief 初始化提交与呈现所需的同步对象。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus InitSynchronization();

		VulkanQueue(const VulkanQueue&) = delete;
		VulkanQueue& operator=(const VulkanQueue&) = delete;

		QueueId GetId() const override { return m_Id; }
		RHIDevice* GetDevice() noexcept override;
		VkSemaphore GetTimeline() const noexcept { return m_Timeline; }
		VkQueue GetHandle() const { return m_Queue; }
		uint32_t GetQueueFamily() const { return m_QueueFamily; }
		const RHIStatus& GetFailure() const { return m_Failure; }

		/**
		 * @brief 只有提交被接受，completion 才有效。
		 *
		 * @param[in] submission 命令列表及提交所需的同步依赖。
		 * @param[out] completion 接收已提交工作的队列完成点，供后续等待或回收使用。
		 *
		 * @return 返回提交受理结果；只有成功时才可使用 completion 跟踪该批次。
		 */
		[[nodiscard]] RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& completion) override;
		/**
		 * @brief 非阻塞查询该队列上的整批提交是否完成。
		 *
		 * @param[in] point 待查询或等待的队列完成点。
		 * @param[out] complete 接收该提交是否已经执行完毕。
		 *
		 * @return 返回查询操作的状态，complete 给出完成点是否已达到。
		 */
		[[nodiscard]] RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override;

		/**
		 * @brief 核对已录制的命令、等待条件及栅栏，把该批次提交给 Vulkan 队列。
		 * 确认命令录制已经完成。
		 * 准备提交描述，再调用已有 Fence::Reset()。
		 * 调用 vkQueueSubmit2()。
		 * 提交成功后，才将命令列表和 Fence 标记为 Pending。
		 *
		 * @param[in,out] commands 已经录制完毕的命令列表。
		 * @param[in,out] fence 收到本次提交完成信号的栅栏。
		 * @param[in] waits 提交前要等待的信号量及阶段。
		 * @param[in] signals 本次提交需触发的信号量。
		 * @param[in] dependencies 本次提交必须等待的队列完成点序列。
		 *
		 * @return 返回队列受理命令的结果；成功表示已提交，GPU 完成情况须另行查询。
		 */
		[[nodiscard]] RHIStatus Submit(VulkanCommandList& commands, VulkanFence& fence,
		    std::span<const VkSemaphoreSubmitInfo> waits = {}, std::span<const VkSemaphoreSubmitInfo> signals = {},
		    std::span<const RHICompletionPoint> dependencies = {});
		[[nodiscard]] RHIStatus SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits,
		    std::span<const VkSemaphoreSubmitInfo> signals = {});

		void NotifyFenceCompleted(VulkanFence& fence);
		RHICompletionPoint GetLastCompletionPoint() const;

		void DiscardPendingSubmissions() { m_Submissions.DiscardPendingSubmissions(); }

	private:
		[[nodiscard]] RHIStatus SubmitInternal(VulkanCommandList* commands, VulkanFence& fence,
		    std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals,
		    std::span<const RHICompletionPoint> dependencies = {});

		/* 原生队列与身份 */
		VulkanDevice& m_Device;
		VkQueue m_Queue;
		uint32_t m_QueueFamily;
		QueueId m_Id{};
		VkSemaphore m_Timeline = VK_NULL_HANDLE;

		/*
		 * 唯一完成账本；Fence 由帧持有，销毁帧前清除 tracker 的借用。
		 * 关闭时丢弃跟踪记录并不能证明对应提交已完成。
		 */
		VulkanSubmissionTracker m_Submissions;

		/* 保存终止性提交失败，不随后续查询恢复。 */
		RHIStatus m_Failure{};
	};

}
