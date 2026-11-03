#include "catpch.h"
#include "VulkanQueue.h"
#include "VulkanCommandList.h"
#include "VulkanFence.h"
#include "VulkanUtils.h"
#include "VulkanDevice.h"
#include "VulkanFrameContext.h"

namespace Catalpa {

    /*
    * 确认命令录制已经完成。
    * 准备提交描述，再调用已有 Fence::Reset()。
    * 调用 vkQueueSubmit2()。
    * 提交成功后，才将命令列表和 Fence 标记为 Pending。
    */
    RHIStatus VulkanQueue::Submit(VulkanCommandList& commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals)
    {
        return SubmitInternal(&commands, fence, waits, signals);
    }

    RHIStatus VulkanQueue::Submit(const RHISubmission& submission, RHICompletionPoint& completion)
    {
        completion = {};
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();

        auto* frame = m_Device.ResolveFrame(submission.frame);

        if (!frame || submission.commands != &frame->GetCommandList())
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanQueue::Submit" };

        const RHIStatus status = frame->Submit(*this, submission.wait, submission.signal);
        if (status.IsSuccess()) completion = GetLastCompletionPoint();
        return status;
    }

    RHICompletionPoint VulkanQueue::GetLastCompletionPoint() const
    {
        return { .device = m_Device.GetDeviceId(), .queue = m_Id, .sequence = m_Submitted };
    }

    void VulkanQueue::NotifyFenceCompleted(VulkanFence& fence)
    {
        CAT_CORE_ASSERT(fence.GetState() == VulkanFenceState::Signaled, "Fence completion must be observed first!");

        const auto it = std::ranges::find(m_Pending, &fence, &PendingSubmission::fence);
        if (it == m_Pending.end()) return;

        m_Completed = std::max(m_Completed, it->sequence);
        std::erase_if(m_Pending, [this](const auto& pending) { return pending.sequence <= m_Completed; });
    }

    RHIStatus VulkanQueue::IsComplete(const RHICompletionPoint& point, bool& complete)
    {
        complete = false;
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();
        if (!m_Failure.IsSuccess()) return m_Failure;

        if (!point.HasValue() || point.device != m_Device.GetDeviceId() || point.queue != m_Id || point.sequence > m_Submitted)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanQueue::IsComplete" };

        if (point.sequence <= m_Completed) {
            complete = true;
            return {};
        }

        const auto it = std::ranges::find(m_Pending, point.sequence, &PendingSubmission::sequence);

        if (it == m_Pending.end())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanQueue::IsComplete" };

        VulkanFence& fence = *it->fence;
        bool signaled = false;
        const RHIStatus status = m_Device.ObserveStatus(fence.Wait(0, signaled));

        if (status.IsSuccess() && signaled) {
            NotifyFenceCompleted(fence);
            complete = true;
        }

        return status;
    }

    RHIStatus VulkanQueue::SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals)
    {
        return SubmitInternal(nullptr, fence, waits, signals);
    }

    RHIStatus VulkanQueue::SubmitInternal(VulkanCommandList* commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals)
    {
        if (!m_Failure.IsSuccess()) return m_Failure;

        if (commands && commands->GetState() != RHICommandListState::Executable) {
            CAT_CORE_ASSERT(false, "Command list must be executable before submission!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanQueue::Submit" };
        }

        if (m_Submitted == std::numeric_limits<uint64_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanQueue::SubmissionSequence" };

        if (fence.GetState() == VulkanFenceState::Signaled) NotifyFenceCompleted(fence);

        // 提交接受后不再扩容，避免接受了 GPU 工作却未能登记完成记录。
        m_Pending.reserve(m_Pending.size() + 1);

        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands ? commands->GetHandle() : VK_NULL_HANDLE,
            .deviceMask = 1
        };

        const VkSubmitInfo2 submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size()),
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = commands ? 1u : 0u,
            .pCommandBufferInfos = commands ? &commandInfo : nullptr,
            .signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size()),
            .pSignalSemaphoreInfos = signals.data()
        };

        if (const RHIStatus status = fence.Reset(); !status.IsSuccess()) return m_Device.ObserveStatus(status);

        const RHIStatus status = m_Device.ObserveStatus(MakeVulkanStatus(vkQueueSubmit2(m_Queue, 1, &submitInfo, fence.GetHandle()), "vkQueueSubmit2"));

        if (!status.IsSuccess()) {
            if (status.code != RHIErrorCode::OutOfMemory) {
                m_Failure = status;
                if (commands) commands->Invalidate();
            }

            return status;
        }

        if (commands) commands->MarkSubmitted();
        fence.MarkSubmitted();
        m_Pending.push_back({ .sequence = ++m_Submitted, .fence = &fence });
        return {};
    }

}