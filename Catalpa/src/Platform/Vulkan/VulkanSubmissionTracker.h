#pragma once

#include "catpch.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class VulkanDevice;
    class VulkanFence;

    // 单个 queue 的唯一提交/完成序列账本；当前仅使用覆盖整批提交的 Fence。
    // Fence 和命令资源引用仍归帧所有，不建立第二个资源退休管理器。
    class VulkanSubmissionTracker {
    public:
        VulkanSubmissionTracker(VulkanDevice& device, QueueId queue);
        VulkanSubmissionTracker(const VulkanSubmissionTracker&) = delete;
        VulkanSubmissionTracker& operator=(const VulkanSubmissionTracker&) = delete;

        // 必须在 Fence reset/vkQueueSubmit2 之前调用；预留成功接受后的登记空间。
        [[nodiscard]] RHIStatus PrepareSubmission(VulkanFence& fence);

        // 只在 vkQueueSubmit2 成功且 Fence 标记 Pending 后调用；不分配内存。
        void AcceptSubmission(VulkanFence& fence) noexcept;

        // 仅接受已经由原生等待确认 Signaled 的 Fence，不以帧号推断完成。
        void NotifyFenceCompleted(VulkanFence& fence);
        [[nodiscard]] RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete);
        [[nodiscard]] RHICompletionPoint GetLastCompletionPoint() const noexcept;

        // 仅关闭/失设备时使用；解除 Fence 借用并永久停止本账本，不伪造完成。
        void DiscardPendingSubmissions() noexcept;

    private:
        [[nodiscard]] RHIStatus ValidateDevice() const;

        struct PendingSubmission {
            uint64_t sequence = 0;
            VulkanFence* fence = nullptr;
        };

        VulkanDevice& m_Device;
        const DeviceId m_DeviceId;
        const QueueId m_QueueId;
        uint64_t m_Submitted = 0;
        uint64_t m_Completed = 0;
        std::vector<PendingSubmission> m_Pending;
        bool m_Discarded = false;
    };

}
