#include "catpch.h"
#include "VulkanFrameContext.h"
#include "VulkanDevice.h"
#include "VulkanQueue.h"
#include "VulkanSwapchain.h"
#include "VulkanDebugUtils.h"

namespace Catalpa {

    VulkanFrameContext::VulkanFrameContext(VulkanDevice& device, uint32_t slot) : m_Device(device), m_Slot(slot), m_Fence(device), m_AcquireSemaphore(device), m_CommandPool(device), m_CommandList(device) {}

    // 先销毁原生命令池，随后成员析构归还命令保留的资源引用。
    VulkanFrameContext::~VulkanFrameContext() { m_CommandPool.Shutdown(); }

    RHIFrameToken VulkanFrameContext::GetToken() const { return { .device = m_Device.GetDeviceId(), .slot = m_Slot, .generation = m_Generation }; }

    RHIStatus VulkanFrameContext::Init(uint32_t queueFamily)
    {
        if (const RHIStatus status = m_CommandPool.Init(queueFamily); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_CommandList.Init(m_CommandPool); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_Fence.Init(true); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_AcquireSemaphore.Init(); !status.IsSuccess()) return status;

        const auto& debug = m_Device.GetDebugUtils();

        if (debug.IsEnabled()) {
            const std::string prefix = "Frame[" + std::to_string(m_Slot) + "]";
            const VkDevice device = m_Device.GetHandle();

            debug.NameObject(device, VK_OBJECT_TYPE_COMMAND_POOL, VulkanHandleToUint64(m_CommandPool.GetHandle()), (prefix + ".CommandPool").c_str());
            debug.NameObject(device, VK_OBJECT_TYPE_COMMAND_BUFFER, VulkanHandleToUint64(m_CommandList.GetHandle()), (prefix + ".Commands").c_str());
            debug.NameObject(device, VK_OBJECT_TYPE_FENCE, VulkanHandleToUint64(m_Fence.GetHandle()), (prefix + ".SubmitFence").c_str());
            debug.NameObject(device, VK_OBJECT_TYPE_SEMAPHORE, VulkanHandleToUint64(m_AcquireSemaphore.GetHandle()), (prefix + ".AcquireReady").c_str());
        }

        return {};
    }

    RHIStatus VulkanFrameContext::PrepareReuse(uint64_t timeoutNanoseconds, bool& ready)
    {
        ready = false;

        if (m_State == State::Acquired || m_Fence.GetState() == VulkanFenceState::Unsubmitted)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::PrepareReuse" };

        if (m_Generation == std::numeric_limits<uint64_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanFrameContext::Generation" };

        bool signaled = false;
        RHIStatus status = m_Fence.Wait(timeoutNanoseconds, signaled);
        if (!status.IsSuccess() || !signaled) return status;

        m_Device.GetGraphicsQueue().NotifyFenceCompleted(m_Fence);

        status = m_CommandPool.Reset();
        if (!status.IsSuccess()) return status;

        m_CommandList.OnPoolReset();
        m_State = State::Available;
        m_ActiveSwapchain = nullptr;
        m_Acquired = {};
        ++m_Generation;
        ready = true;
        return {};
    }

    RHIStatus VulkanFrameContext::Acquire(VulkanSwapchain& swapchain, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired)
    {
        acquired = {};

        if (m_State != State::Available)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::Acquire" };

        const RHIStatus status = swapchain.AcquireNative(m_AcquireSemaphore, timeoutNanoseconds, acquired);
        if (!status.IsSuccess() || !acquired.HasImage()) return status;

        acquired.view = swapchain.GetImageViewHandle(acquired.imageIndex);
        acquired.acquireReady = { .source = GetToken(), .kind = RHIGpuDependencyKind::AcquireReady };
        acquired.renderFinished = { .source = GetToken(), .kind = RHIGpuDependencyKind::RenderFinished };

        m_Acquired = acquired;
        m_ActiveSwapchain = &swapchain;
        m_State = State::Acquired;
        return {};
    }

    RHIStatus VulkanFrameContext::Submit(VulkanQueue& queue, const RHIGpuDependency& wait, const RHIGpuDependency& signal)
    {
        if (m_State != State::Acquired || !m_ActiveSwapchain || wait != m_Acquired.acquireReady || signal != m_Acquired.renderFinished)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::Submit" };

        const VkSemaphoreSubmitInfo waitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_AcquireSemaphore.GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const VkSemaphoreSubmitInfo signalInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_ActiveSwapchain->GetPresentSemaphore(m_Acquired.imageIndex).GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const RHIStatus status = queue.Submit(m_CommandList, m_Fence, { &waitInfo, 1 }, { &signalInfo, 1 });
        if (status.IsSuccess()) m_State = State::Submitted;
        return status;
    }

    RHIStatus VulkanFrameContext::Present(VulkanSwapchain& swapchain, const RHIAcquireResult& acquired, RHIPresentOutcome& outcome)
    {
        outcome = RHIPresentOutcome::NotQueued;

        if (m_State != State::Submitted || m_ActiveSwapchain != &swapchain || acquired != m_Acquired)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::Present" };

        const RHIStatus status = swapchain.PresentNative(acquired, outcome);

        switch (outcome) {
        case RHIPresentOutcome::Queued:
        case RHIPresentOutcome::Suboptimal:
        case RHIPresentOutcome::OutOfDate:
        case RHIPresentOutcome::SurfaceLost:
            m_ActiveSwapchain = nullptr;
            m_Acquired = {};
            break;
        default:
            break;
        }

        return status;
    }

    RHIStatus VulkanFrameContext::CancelAcquire(VulkanQueue& queue, VulkanSwapchain& swapchain, RHIAcquireResult& acquired)
    {
        if (m_State != State::Acquired || m_ActiveSwapchain != &swapchain || acquired != m_Acquired)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanFrameContext::CancelAcquire" };

        const VkSemaphoreSubmitInfo waitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_AcquireSemaphore.GetHandle(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const RHIStatus status = queue.SubmitSynchronization(m_Fence, { &waitInfo, 1 });
        if (!status.IsSuccess()) return status;

        m_CommandList.Invalidate();
        m_State = State::Submitted;
        m_ActiveSwapchain = nullptr;
        m_Acquired = {};
        swapchain.RequestRecreate();
        acquired = {};
        return {};
    }

}