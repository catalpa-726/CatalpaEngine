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
    RHIStatus VulkanQueue::Submit(VulkanCommandList& commands, VulkanFence& fence)
    {
        if (!m_Failure.IsSuccess()) return m_Failure;

        if (commands.GetState() != RHICommandListState::Executable) {
            CAT_CORE_ASSERT(false, "Command list must be executable before submission!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanQueue::Submit" };
        }

        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands.GetHandle(),
            .deviceMask = 1
        };

        const VkSubmitInfo2 submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo
        };

        if (const RHIStatus status = fence.Reset(); !status.IsSuccess()) return status;

        const RHIStatus status = MakeVulkanStatus(vkQueueSubmit2(m_Queue, 1, &submitInfo, fence.GetHandle()), "vkQueueSubmit2");

        if (!status.IsSuccess()) {
            if (status.code != RHIErrorCode::OutOfMemory) {
                m_Failure = status;
                commands.Invalidate();
            }
            return status;
        }

        commands.MarkSubmitted();
        fence.MarkSubmitted();
        return {};
    }

}