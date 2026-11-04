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
        return m_Submissions.GetLastCompletionPoint();
    }

    void VulkanQueue::NotifyFenceCompleted(VulkanFence& fence)
    {
        m_Submissions.NotifyFenceCompleted(fence);
    }

    RHIStatus VulkanQueue::IsComplete(const RHICompletionPoint& point, bool& complete)
    {
        complete = false;
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();
        if (!m_Failure.IsSuccess()) return m_Failure;

        return m_Submissions.IsComplete(point, complete);
    }

    RHIStatus VulkanQueue::SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals)
    {
        return SubmitInternal(nullptr, fence, waits, signals);
    }

    RHIStatus VulkanQueue::SubmitInternal(VulkanCommandList* commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals)
    {
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();
        if (!m_Failure.IsSuccess()) return m_Failure;

        // nullptr 是取消 Acquire 的纯同步提交，不包含资源状态更新。
        if (commands) {
            if (const RHIStatus status = commands->ValidateResourceStatePlan(); !status.IsSuccess())
                return status;
        }

        if (const auto status = m_Submissions.PrepareSubmission(fence); !status.IsSuccess()) return status;

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

        if (commands) {
            commands->CommitResourceStatePlan();
            commands->MarkSubmitted();
        }

        fence.MarkSubmitted();
        m_Submissions.AcceptSubmission(fence);
        return {};
    }

}
