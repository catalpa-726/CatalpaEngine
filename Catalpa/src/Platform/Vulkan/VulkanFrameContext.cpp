#include "catpch.h"
#include "Log/Log.h"
#include "VulkanFrameContext.h"
#include "VulkanQueue.h"
#include "VulkanSwapchain.h"

namespace Catalpa {

    VulkanFrameContext::VulkanFrameContext(const VulkanDevice& device) : m_Fence(device), m_AcquireSemaphore(device), m_CommandPool(device) {}

    RHIStatus VulkanFrameContext::Init(uint32_t queueFamily)
    {
        if (const RHIStatus status = m_CommandPool.Init(queueFamily); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_CommandList.Init(m_CommandPool); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_Fence.Init(true); !status.IsSuccess()) return status;
        return m_AcquireSemaphore.Init();
    }

    // 	等待提交完成，再重置命令池；`ready` 表示本次准备是否完成
    RHIStatus VulkanFrameContext::PrepareReuse(uint64_t timeoutNanoseconds, bool& ready)
    {
        ready = false;

        if (m_State == State::Acquired || m_Fence.GetState() == VulkanFenceState::Unsubmitted)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::PrepareReuse" };

        bool signaled = false;
        RHIStatus status = m_Fence.Wait(timeoutNanoseconds, signaled);
        if (!status.IsSuccess() || !signaled) return status;

        status = m_CommandPool.Reset();
        if (!status.IsSuccess()) return status;

        m_CommandList.OnPoolReset();
        m_State = State::Available;
        ready = true;
        return {};
    }

    RHIStatus VulkanFrameContext::Acquire(VulkanSwapchain& swapchain, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired)
    {
        acquired = {};

        if (m_State != State::Available) {
            CAT_CORE_ASSERT(false, "Frame must be available before acquiring an image!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::Acquire" };
        }

        const RHIStatus status = swapchain.Acquire(m_AcquireSemaphore, timeoutNanoseconds, acquired);
        if (status.IsSuccess() && acquired.HasImage()) m_State = State::Acquired;
        return status;
    }

    RHIStatus VulkanFrameContext::Submit(VulkanQueue& queue, VulkanBinarySemaphore& presentSemaphore)
    {
        if (m_State != State::Acquired) {
            CAT_CORE_ASSERT(false, "Frame must have an acquired image before submission!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::Submit" };
        }

        const VkSemaphoreSubmitInfo waitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_AcquireSemaphore.GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const VkSemaphoreSubmitInfo signalInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = presentSemaphore.GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const RHIStatus status = queue.Submit(m_CommandList, m_Fence, { &waitInfo, 1 }, { &signalInfo, 1 });
        if (status.IsSuccess()) m_State = State::Submitted;
        return status;
    }

    RHIStatus VulkanFrameContext::CancelAcquire(VulkanQueue& queue, VulkanSwapchain& swapchain, RHIAcquireResult& acquired)
    {
        if (m_State != State::Acquired || !acquired.HasImage()) {
            CAT_CORE_ASSERT(false, "Only an acquired, unsubmitted frame can be cancelled!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::CancelAcquire" };
        }

        const VkSemaphoreSubmitInfo waitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_AcquireSemaphore.GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const RHIStatus status = queue.SubmitSynchronization(m_Fence, { &waitInfo, 1 });
        if (!status.IsSuccess()) return status;

        m_CommandList.Invalidate();
        m_State = State::Submitted;
        swapchain.RequestRecreate();
        acquired = {};
        return {};
    }

}