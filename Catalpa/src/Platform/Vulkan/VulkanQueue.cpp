#include "catpch.h"
#include "VulkanQueue.h"

#include "VulkanCommandList.h"
#include "VulkanFence.h"
#include "VulkanUtils.h"
#include "VulkanDevice.h"
#include "VulkanFrameContext.h"
#include "VulkanAuxiliaryContext.h"

namespace Catalpa {
	VulkanQueue::~VulkanQueue() {
		if (m_Timeline)
			vkDestroySemaphore(m_Device.GetHandle(), m_Timeline, nullptr);
	}

	RHIDevice* VulkanQueue::GetDevice() noexcept {
		return &m_Device;
	}

	RHIStatus VulkanQueue::InitSynchronization() {
		if (!m_Device.HasTimelineSemaphores())
			return {};
		const VkSemaphoreTypeCreateInfo type{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		    .initialValue = 0};
		const VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
		return MakeVulkanStatus(
		    vkCreateSemaphore(m_Device.GetHandle(), &info, nullptr, &m_Timeline), "vkCreateSemaphore/timeline");
	}

	RHIStatus VulkanQueue::Submit(VulkanCommandList& commands, VulkanFence& fence,
	    std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals,
	    std::span<const RHICompletionPoint> dependencies) {
		return SubmitInternal(&commands, fence, waits, signals, dependencies);
	}

	RHIStatus VulkanQueue::Submit(const RHISubmission& submission, RHICompletionPoint& completion) {
		completion = {};
		if (!m_Device.GetFailure().IsSuccess())
			return m_Device.GetFailure();

		auto* frame = m_Device.ResolveFrame(submission.frame);

		if (!frame)
			return {.code = RHIErrorCode::InvalidDescription, .operation = "VulkanQueue::Submit"};
		if (submission.commands != &frame->GetCommandList()) {
			auto* auxiliary = m_Device.ResolveAuxiliary(submission.frame, submission.commands, m_Id);
			if (!auxiliary || submission.wait.HasValue() || submission.signal.HasValue())
				return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/AuxiliaryIdentity"};
			const auto status = Submit(auxiliary->Commands(), auxiliary->Fence(), {}, {}, submission.dependencies);
			if (status.IsSuccess())
				completion = GetLastCompletionPoint();
			return status;
		}
		if (m_Id != m_Device.GetGraphicsQueue().GetId())
			return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/PresentationQueue"};

		const RHIStatus status = frame->Submit(*this, submission.wait, submission.signal, submission.dependencies);
		if (status.IsSuccess())
			completion = GetLastCompletionPoint();
		return status;
	}

	RHICompletionPoint VulkanQueue::GetLastCompletionPoint() const {
		return m_Submissions.GetLastCompletionPoint();
	}

	void VulkanQueue::NotifyFenceCompleted(VulkanFence& fence) {
		m_Submissions.NotifyFenceCompleted(fence);
	}

	RHIStatus VulkanQueue::IsComplete(const RHICompletionPoint& point, bool& complete) {
		complete = false;
		if (!m_Device.GetFailure().IsSuccess())
			return m_Device.GetFailure();
		if (!m_Failure.IsSuccess())
			return m_Failure;

		return m_Submissions.IsComplete(point, complete);
	}

	RHIStatus VulkanQueue::SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits,
	    std::span<const VkSemaphoreSubmitInfo> signals) {
		return SubmitInternal(nullptr, fence, waits, signals);
	}

	/**
	 * @brief 在同一提交路径上处理普通命令与纯同步批次，仅在队列受理后发布状态。
	 * 命令提交与纯同步提交共用的入口
	 *
	 * @param[in,out] commands 待执行的命令列表；为空时只提交同步操作，以收尾已获取的图像。
	 * @param[in,out] fence 关联本次提交的原生栅栏，用于跟踪执行完成。
	 * @param[in] waits 提交执行前等待的原生信号量及等待阶段。
	 * @param[in] signals 该批次提交时请求触发的原生信号量及阶段。
	 * @param[in] dependencies 本次提交必须等待的队列完成点序列。
	 *
	 * @return 返回命令或纯同步批次的受理结果；状态账本在原生提交成功后才发布。
	 */
	RHIStatus VulkanQueue::SubmitInternal(VulkanCommandList* commands, VulkanFence& fence,
	    std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals,
	    std::span<const RHICompletionPoint> dependencies) try {
		if (!m_Device.GetFailure().IsSuccess())
			return m_Device.GetFailure();
		if (!m_Failure.IsSuccess())
			return m_Failure;

		/* nullptr 是取消 Acquire 的纯同步提交，不包含资源状态更新。 */
		if (commands) {
			if (commands->GetQueueFamily() != m_QueueFamily)
				return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/CommandPoolFamily"};
			if (const RHIStatus status = commands->ValidateResourceStatePlan(); !status.IsSuccess())
				return status;
			for (const auto required : commands->GetRequiredGpuDependencies()) {
				if (required.queue == m_Id)
					continue;
				if (std::none_of(dependencies.begin(), dependencies.end(), [&](const auto& supplied) {
					    return supplied.device == required.device && supplied.queue == required.queue &&
					        supplied.sequence >= required.sequence;
				    }))
					return {RHIErrorCode::InvalidState, 0, "VulkanQueue/MissingFrontierWait"};
			}
		}

		/* 在改变栅栏之前为提交记录预留空间，并把其他队列的已受理完成点折算为时间线等待。 */
		if (const auto status = m_Submissions.PrepareSubmission(fence); !status.IsSuccess())
			return status;
		std::vector<VkSemaphoreSubmitInfo> waitInfos(waits.begin(), waits.end()),
		    signalInfos(signals.begin(), signals.end());
		for (const auto point : dependencies) {
			auto* base = m_Device.GetQueue(point.queue);
			auto* source = dynamic_cast<VulkanQueue*>(base);
			if (!point.HasValue() || point.device != m_Device.GetDeviceId() || !source ||
			    point.sequence > source->GetLastCompletionPoint().sequence)
				return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/UnacceptedDependency"};
			if (point.queue == m_Id)
				continue;
			if (!source->GetTimeline())
				return {RHIErrorCode::Unsupported, 0, "VulkanQueue/TimelineRequired"};
			const auto existing = std::find_if(waitInfos.begin(), waitInfos.end(),
			    [&](const auto& w) { return w.semaphore == source->GetTimeline(); });
			if (existing != waitInfos.end())
				existing->value = std::max(existing->value, point.sequence);
			else
				waitInfos.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				    .semaphore = source->GetTimeline(),
				    .value = point.sequence,
				    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
		}
		if (m_Timeline)
			signalInfos.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			    .semaphore = m_Timeline,
			    .value = GetLastCompletionPoint().sequence + 1,
			    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});

		const VkCommandBufferSubmitInfo commandInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		    .commandBuffer = commands ? commands->GetHandle() : VK_NULL_HANDLE,
		    .deviceMask = 1};

		const VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size()),
		    .pWaitSemaphoreInfos = waitInfos.data(),
		    .commandBufferInfoCount = commands ? 1u : 0u,
		    .pCommandBufferInfos = commands ? &commandInfo : nullptr,
		    .signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size()),
		    .pSignalSemaphoreInfos = signalInfos.data()};

		/* 等等待和信号列表全部就绪后再重置栅栏，随即提交原生命令。 */
		if (const RHIStatus status = fence.Reset(); !status.IsSuccess())
			return m_Device.ObserveStatus(status);

		const RHIStatus status = m_Device.ObserveStatus(
		    MakeVulkanStatus(vkQueueSubmit2(m_Queue, 1, &submitInfo, fence.GetHandle()), "vkQueueSubmit2"));

		if (!status.IsSuccess()) {
			if (status.code != RHIErrorCode::OutOfMemory) {
				m_Failure = status;
				if (commands)
					commands->Invalidate();
			}

			return status;
		}

		/* 至此原生队列已受理。发布完成点和资源状态，并将命令列表转入待完成状态。 */
		fence.MarkSubmitted();
		m_Submissions.AcceptSubmission(fence);
		if (commands) {
			commands->CommitResourceStatePlan(GetLastCompletionPoint());
			commands->MarkSubmitted();
		}
		return {};
	} catch (const std::bad_alloc&) {
		return {RHIErrorCode::OutOfMemory, 0, "VulkanQueue/SubmissionAllocation"};
	}
}
