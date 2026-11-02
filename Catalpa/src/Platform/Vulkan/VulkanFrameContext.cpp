#include "catpch.h"
#include "VulkanFrameContext.h"

namespace Catalpa {

    VulkanFrameContext::VulkanFrameContext(const VulkanDevice& device) : m_Fence(device), m_AcquireSemaphore(device), m_CommandPool(device) {}

    RHIStatus VulkanFrameContext::Init(uint32_t queueFamily)
    {
        if (const RHIStatus status = m_CommandPool.Init(queueFamily); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_CommandPool.AllocatePrimary(m_CommandBuffer); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_Fence.Init(true); !status.IsSuccess()) return status;
        return m_AcquireSemaphore.Init();
    }

    // 	等待提交完成，再重置命令池；`ready` 表示本次准备是否完成
    RHIStatus VulkanFrameContext::PrepareReuse(uint64_t timeoutNanoseconds, bool& ready)
    {
        ready = false;

        if (m_Fence.GetState() == VulkanFenceState::Unsubmitted)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::PrepareReuse" };

        bool signaled = false;
        RHIStatus status = m_Fence.Wait(timeoutNanoseconds, signaled);
        if (!status.IsSuccess() || !signaled) return status;

        status = m_CommandPool.Reset();
        ready = status.IsSuccess();
        return status;
    }

}