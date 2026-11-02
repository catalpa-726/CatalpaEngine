#include "catpch.h"
#include "VulkanQueue.h"
#include "VulkanCommandList.h"
#include "VulkanFence.h"
#include "VulkanUtils.h"

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

        if (const RHIStatus status = fence.Reset(); !status.IsSuccess()) return status;

        const RHIStatus status = MakeVulkanStatus(vkQueueSubmit2(m_Queue, 1, &submitInfo, fence.GetHandle()), "vkQueueSubmit2");

        if (!status.IsSuccess()) {
            if (status.code != RHIErrorCode::OutOfMemory) {
                m_Failure = status;
                if (commands) commands->Invalidate();
            }

            return status;
        }

        if (commands) commands->MarkSubmitted();
        fence.MarkSubmitted();
        return {};
    }

}