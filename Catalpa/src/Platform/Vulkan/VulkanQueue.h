#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIQueue.h"
#include "VulkanSubmissionTracker.h"

// 包装已有原生队列，提供提交入口，保存队列发生的终止性失败
namespace Catalpa {

    class VulkanDevice;
    class VulkanCommandList;
    class VulkanFence;

    class VulkanQueue : public RHIQueue {
    public:
        // 借用设备和原生队列
        VulkanQueue(VulkanDevice& device, VkQueue queue, uint32_t queueFamily, QueueId id)
            : m_Device(device), m_Queue(queue), m_QueueFamily(queueFamily), m_Id(id), m_Submissions(device, id) {}

        VulkanQueue(const VulkanQueue&) = delete;
        VulkanQueue& operator=(const VulkanQueue&) = delete;

        // 队列身份与状态
        QueueId GetId() const override { return m_Id; }
        VkQueue GetHandle() const { return m_Queue; }
        uint32_t GetQueueFamily() const { return m_QueueFamily; }
        const RHIStatus& GetFailure() const { return m_Failure; }

        // 公共提交与完成查询
        [[nodiscard]] RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& completion) override;
        [[nodiscard]] RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override;

        // 后端入口，继续使用现有命令、Fence 和信号量。
        [[nodiscard]] RHIStatus Submit(VulkanCommandList& commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits = {}, std::span<const VkSemaphoreSubmitInfo> signals = {});
        [[nodiscard]] RHIStatus SubmitSynchronization(VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals = {});

        // 将已观察到的 Fence 完成保存为持久的序列记录。
        void NotifyFenceCompleted(VulkanFence& fence);
        RHICompletionPoint GetLastCompletionPoint() const;

        // 仅用于停止使用队列后的关闭流程；不表示这些提交正常完成。
        void DiscardPendingSubmissions() { m_Submissions.DiscardPendingSubmissions(); }

    private:
        // 命令提交与纯同步提交共用的入口
        [[nodiscard]] RHIStatus SubmitInternal(VulkanCommandList* commands, VulkanFence& fence, std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals);

        // 原生队列与身份
        VulkanDevice& m_Device;
        VkQueue m_Queue;
        uint32_t m_QueueFamily;
        QueueId m_Id{};

        // 唯一完成账本；Fence 由帧持有，销毁帧前清除 tracker 的借用。
        VulkanSubmissionTracker m_Submissions;

        // 保存终止性提交失败，不随后续查询恢复。
        RHIStatus m_Failure{};
    };

}
