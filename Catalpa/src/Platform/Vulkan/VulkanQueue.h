#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIQueue.h"

// 包装已有原生队列，提供提交入口，保存队列发生的终止性失败
namespace Catalpa {

    class VulkanDevice;
    class VulkanCommandList;
    class VulkanFence;

    class VulkanQueue : public RHIQueue {
    public:
        VulkanQueue(VulkanDevice& device, VkQueue queue, uint32_t queueFamily, QueueId id) : m_Device(device), m_Queue(queue), m_QueueFamily(queueFamily), m_Id(id) {}

        VulkanQueue(const VulkanQueue&) = delete;
        VulkanQueue& operator=(const VulkanQueue&) = delete;

        QueueId GetId() const override { return m_Id; }
        VkQueue GetHandle() const { return m_Queue; }
        uint32_t GetQueueFamily() const { return m_QueueFamily; }
        const RHIStatus& GetFailure() const { return m_Failure; }

        [[nodiscard]] RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& completion) override;
        [[nodiscard]] RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override;

        // 后端入口，继续使用现有命令、Fence 和信号量。
        [[nodiscard]] RHIStatus Submit(VulkanCommandList& commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits = {}, std::span<const VkSemaphoreSubmitInfo> signals = {});
        [[nodiscard]] RHIStatus SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals = {});

        // 将已观察到的 Fence 完成保存为持久的序列记录。
        void NotifyFenceCompleted(VulkanFence& fence);
        RHICompletionPoint GetLastCompletionPoint() const;

        // 仅用于停止使用队列后的关闭流程；不表示这些提交正常完成。
        void DiscardPendingSubmissions() { m_Pending.clear(); }

    private:
        [[nodiscard]] RHIStatus SubmitInternal(VulkanCommandList* commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals);

        struct PendingSubmission {
            uint64_t sequence = 0;
            VulkanFence* fence = nullptr;
        };

        VulkanDevice& m_Device;
        VkQueue m_Queue;
        uint32_t m_QueueFamily;
        QueueId m_Id{};

        uint64_t m_Submitted = 0;
        uint64_t m_Completed = 0;
        std::vector<PendingSubmission> m_Pending;
        RHIStatus m_Failure{};
    };

}