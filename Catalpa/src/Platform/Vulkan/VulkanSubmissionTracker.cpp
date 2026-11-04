#include "catpch.h"
#include "VulkanSubmissionTracker.h"
#include "VulkanDevice.h"
#include "VulkanFence.h"
#include "Log/Log.h"

namespace Catalpa {

    VulkanSubmissionTracker::VulkanSubmissionTracker(VulkanDevice& device, QueueId queue)
        : m_Device(device), m_DeviceId(device.GetDeviceId()), m_QueueId(queue) {}

    RHIStatus VulkanSubmissionTracker::ValidateDevice() const
    {
        // 失设备优先于历史完成缓存；设备重建后的旧 epoch 不能继续查询或提交。
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();
        if (m_Discarded || !m_DeviceId.HasValue() || !m_QueueId.HasValue()
            || m_Device.GetDeviceId() != m_DeviceId)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanSubmissionTracker::DeviceEpoch" };
        return {};
    }

    RHIStatus VulkanSubmissionTracker::PrepareSubmission(VulkanFence& fence)
    {
        if (const auto status = ValidateDevice(); !status.IsSuccess()) return status;
        if (fence.GetHandle() == VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanSubmissionTracker::FenceMissing" };
        if (m_Submitted == std::numeric_limits<uint64_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanSubmissionTracker::Sequence" };
        if (fence.GetState() == VulkanFenceState::Signaled) NotifyFenceCompleted(fence);
        if (fence.GetState() == VulkanFenceState::Pending
            || std::ranges::find(m_Pending, &fence, &PendingSubmission::fence) != m_Pending.end())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanSubmissionTracker::FencePending" };

        // 所有可能的分配失败发生在原生提交前；提交失败不会消耗序列。
        if (m_Pending.size() == m_Pending.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanSubmissionTracker::Reserve" };
        try { m_Pending.reserve(m_Pending.size() + 1); }
        catch (const std::bad_alloc&) {
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanSubmissionTracker::Reserve" };
        }
        catch (const std::length_error&) {
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanSubmissionTracker::Reserve" };
        }
        return {};
    }

    void VulkanSubmissionTracker::AcceptSubmission(VulkanFence& fence) noexcept
    {
        CAT_CORE_ASSERT(!m_Discarded && m_Pending.size() < m_Pending.capacity()
            && m_Submitted != std::numeric_limits<uint64_t>::max()
            && fence.GetState() == VulkanFenceState::Pending, "Submission must be prepared and accepted before tracking!");
        // 主线程串行调用，PrepareSubmission 与此处之间不允许插入其他提交。
        m_Pending.push_back({ .sequence = ++m_Submitted, .fence = &fence });
    }

    void VulkanSubmissionTracker::NotifyFenceCompleted(VulkanFence& fence)
    {
        if (!ValidateDevice().IsSuccess()) return;
        CAT_CORE_ASSERT(fence.GetState() == VulkanFenceState::Signaled, "Fence completion must be observed first!");
        if (fence.GetState() != VulkanFenceState::Signaled) return;
        const auto it = std::ranges::find(m_Pending, &fence, &PendingSubmission::fence);
        if (it == m_Pending.end()) return;

        // 同一 queue 后续整批 Fence 完成覆盖先前提交；不代表其他 queue 或呈现完成。
        m_Completed = std::max(m_Completed, it->sequence);
        std::erase_if(m_Pending, [this](const auto& pending) { return pending.sequence <= m_Completed; });
    }

    RHIStatus VulkanSubmissionTracker::IsComplete(const RHICompletionPoint& point, bool& complete)
    {
        complete = false;
        if (const auto status = ValidateDevice(); !status.IsSuccess()) return status;
        if (!point.HasValue() || point.device != m_DeviceId || point.queue != m_QueueId || point.sequence > m_Submitted)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanSubmissionTracker::IsComplete" };
        if (point.sequence <= m_Completed) {
            complete = true;
            return {};
        }

        const auto it = std::ranges::find(m_Pending, point.sequence, &PendingSubmission::sequence);
        if (it == m_Pending.end())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanSubmissionTracker::IsComplete" };
        VulkanFence& fence = *it->fence;
        bool signaled = false;
        const auto status = m_Device.ObserveStatus(fence.Wait(0, signaled));
        if (status.IsSuccess() && signaled) {
            NotifyFenceCompleted(fence);
            complete = point.sequence <= m_Completed;
        }
        return status;
    }

    RHICompletionPoint VulkanSubmissionTracker::GetLastCompletionPoint() const noexcept
    {
        return { .device = m_DeviceId, .queue = m_QueueId, .sequence = m_Submitted };
    }

    void VulkanSubmissionTracker::DiscardPendingSubmissions() noexcept
    {
        m_Pending.clear();
        m_Discarded = true;
    }

}
