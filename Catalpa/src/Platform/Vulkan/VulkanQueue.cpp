#include "catpch.h"
#include "VulkanQueue.h"
#include "VulkanCommandList.h"
#include "VulkanFence.h"
#include "VulkanUtils.h"
#include "VulkanDevice.h"
#include "VulkanFrameContext.h"
#include "VulkanAuxiliaryContext.h"

namespace Catalpa {
    VulkanQueue::~VulkanQueue() {
        if (m_Timeline)
            vkDestroySemaphore(m_Device.GetHandle(), m_Timeline, nullptr);
    }
    RHIDevice* VulkanQueue::GetDevice() noexcept {
        return &m_Device;
    }
    RHIStatus VulkanQueue::InitSynchronization() {
        if (!m_Device.HasTimelineSemaphores())
            return {};
        const VkSemaphoreTypeCreateInfo type{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                             .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                             .initialValue = 0};
        const VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
        return MakeVulkanStatus(vkCreateSemaphore(m_Device.GetHandle(), &info, nullptr, &m_Timeline),
                                "vkCreateSemaphore/timeline");
    }

    /*
     * 确认命令录制已经完成。
     * 准备提交描述，再调用已有 Fence::Reset()。
     * 调用 vkQueueSubmit2()。
     * 提交成功后，才将命令列表和 Fence 标记为 Pending。
     */
    RHIStatus VulkanQueue::Submit(VulkanCommandList& commands, VulkanFence& fence,
                                  std::span<const VkSemaphoreSubmitInfo> waits,
                                  std::span<const VkSemaphoreSubmitInfo> signals,
                                  std::span<const RHICompletionPoint> dependencies) {
        return SubmitInternal(&commands, fence, waits, signals, dependencies);
    }

    RHIStatus VulkanQueue::Submit(const RHISubmission& submission, RHICompletionPoint& completion)
    {
        completion = {};
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();

        auto* frame = m_Device.ResolveFrame(submission.frame);

        if (!frame)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanQueue::Submit" };
        if (submission.commands != &frame->GetCommandList()) {
            auto* auxiliary = m_Device.ResolveAuxiliary(submission.frame, submission.commands, m_Id);
            if (!auxiliary || submission.wait.HasValue() || submission.signal.HasValue())
                return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/AuxiliaryIdentity"};
            const auto status =
                Submit(auxiliary->Commands(), auxiliary->Fence(), {}, {}, submission.dependencies);
            if (status.IsSuccess())
                completion = GetLastCompletionPoint();
            return status;
        }
        if (m_Id != m_Device.GetGraphicsQueue().GetId())
            return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/PresentationQueue"};

        const RHIStatus status =
            frame->Submit(*this, submission.wait, submission.signal, submission.dependencies);
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

    RHIStatus VulkanQueue::SubmitInternal(VulkanCommandList* commands, VulkanFence& fence,
                                          std::span<const VkSemaphoreSubmitInfo> waits,
                                          std::span<const VkSemaphoreSubmitInfo> signals,
                                          std::span<const RHICompletionPoint> dependencies) try {
        if (!m_Device.GetFailure().IsSuccess()) return m_Device.GetFailure();
        if (!m_Failure.IsSuccess()) return m_Failure;

        // nullptr 是取消 Acquire 的纯同步提交，不包含资源状态更新。
        if (commands) {
            if (commands->GetQueueFamily() != m_QueueFamily)
                return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/CommandPoolFamily"};
            if (const RHIStatus status = commands->ValidateResourceStatePlan(); !status.IsSuccess())
                return status;
            for (const auto required : commands->GetRequiredGpuDependencies()) {
                if (required.queue == m_Id)
                    continue;
                if (std::none_of(dependencies.begin(), dependencies.end(), [&](const auto& supplied) {
                        return supplied.device == required.device && supplied.queue == required.queue &&
                               supplied.sequence >= required.sequence;
                    }))
                    return {RHIErrorCode::InvalidState, 0, "VulkanQueue/MissingFrontierWait"};
            }
        }

        if (const auto status = m_Submissions.PrepareSubmission(fence); !status.IsSuccess()) return status;
        std::vector<VkSemaphoreSubmitInfo> waitInfos(waits.begin(), waits.end()),
            signalInfos(signals.begin(), signals.end());
        for (const auto point : dependencies) {
            auto* base = m_Device.GetQueue(point.queue);
            auto* source = dynamic_cast<VulkanQueue*>(base);
            if (!point.HasValue() || point.device != m_Device.GetDeviceId() || !source ||
                point.sequence > source->GetLastCompletionPoint().sequence)
                return {RHIErrorCode::InvalidDescription, 0, "VulkanQueue/UnacceptedDependency"};
            if (point.queue == m_Id)
                continue;
            if (!source->GetTimeline())
                return {RHIErrorCode::Unsupported, 0, "VulkanQueue/TimelineRequired"};
            const auto existing = std::find_if(waitInfos.begin(), waitInfos.end(), [&](const auto& w) {
                return w.semaphore == source->GetTimeline();
            });
            if (existing != waitInfos.end())
                existing->value = std::max(existing->value, point.sequence);
            else
                waitInfos.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .semaphore = source->GetTimeline(),
                                     .value = point.sequence,
                                     .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
        }
        if (m_Timeline)
            signalInfos.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                   .semaphore = m_Timeline,
                                   .value = GetLastCompletionPoint().sequence + 1,
                                   .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});

        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands ? commands->GetHandle() : VK_NULL_HANDLE,
            .deviceMask = 1
        };

        const VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                       .waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size()),
                                       .pWaitSemaphoreInfos = waitInfos.data(),
                                       .commandBufferInfoCount = commands ? 1u : 0u,
                                       .pCommandBufferInfos = commands ? &commandInfo : nullptr,
                                       .signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size()),
                                       .pSignalSemaphoreInfos = signalInfos.data()};

        if (const RHIStatus status = fence.Reset(); !status.IsSuccess()) return m_Device.ObserveStatus(status);

        const RHIStatus status = m_Device.ObserveStatus(MakeVulkanStatus(vkQueueSubmit2(m_Queue, 1, &submitInfo, fence.GetHandle()), "vkQueueSubmit2"));

        if (!status.IsSuccess()) {
            if (status.code != RHIErrorCode::OutOfMemory) {
                m_Failure = status;
                if (commands) commands->Invalidate();
            }

            return status;
        }

        fence.MarkSubmitted();
        m_Submissions.AcceptSubmission(fence);
        if (commands) {
            commands->CommitResourceStatePlan(GetLastCompletionPoint());
            commands->MarkSubmitted();
        }
        return {};
    } catch (const std::bad_alloc&) {
        return {RHIErrorCode::OutOfMemory, 0, "VulkanQueue/SubmissionAllocation"};
    }
}
