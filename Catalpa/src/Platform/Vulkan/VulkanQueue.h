#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

// 包装已有原生队列，提供提交入口，保存队列发生的终止性失败
namespace Catalpa {

    class VulkanCommandList;
    class VulkanFence;

    class VulkanQueue {
    public:
        VulkanQueue(VkQueue queue, uint32_t queueFamily) : m_Queue(queue), m_QueueFamily(queueFamily) {}

        VulkanQueue(const VulkanQueue&) = delete;
        VulkanQueue& operator=(const VulkanQueue&) = delete;

        // 主线程调用；命令列表和 Fence 必须来自同一设备，命令池队列族必须匹配。
        [[nodiscard]] RHIStatus Submit(
            VulkanCommandList& commands, 
            VulkanFence& fence, 
            std::span<const VkSemaphoreSubmitInfo> waits = {}, 
            std::span<const VkSemaphoreSubmitInfo> signals = {});

        VkQueue GetHandle() const { return m_Queue; }
        uint32_t GetQueueFamily() const { return m_QueueFamily; }

    private:
        VkQueue m_Queue;
        uint32_t m_QueueFamily;
        RHIStatus m_Failure{};
    };

}