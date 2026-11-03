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

        const RHIStatus& GetFailure() const { return m_Failure; }

        // 主线程调用；命令列表和 Fence 必须来自同一设备，命令池队列族必须匹配。
        [[nodiscard]] RHIStatus Submit(
            VulkanCommandList& commands, 
            VulkanFence& fence, 
            std::span<const VkSemaphoreSubmitInfo> waits = {}, 
            std::span<const VkSemaphoreSubmitInfo> signals = {});

        // 不提交命令缓冲，只提交信号量等待或通知，并使用 Fence 观察完成。
        [[nodiscard]] RHIStatus SubmitSynchronization(
            VulkanFence& fence, 
            std::span<const VkSemaphoreSubmitInfo> waits,
            std::span<const VkSemaphoreSubmitInfo> signals = {});

        VkQueue GetHandle() const { return m_Queue; }
        uint32_t GetQueueFamily() const { return m_QueueFamily; }

    private:
        // 集中实现两种提交，复用已有的 Fence、错误处理和提交状态逻辑。
        [[nodiscard]] RHIStatus SubmitInternal(
            VulkanCommandList* commands, 
            VulkanFence& fence, 
            std::span<const VkSemaphoreSubmitInfo> waits, 
            std::span<const VkSemaphoreSubmitInfo> signals);

    private:
        VkQueue m_Queue;
        uint32_t m_QueueFamily;
        RHIStatus m_Failure{};
    };

}