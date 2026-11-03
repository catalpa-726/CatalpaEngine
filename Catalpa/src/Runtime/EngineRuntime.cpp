#include "catpch.h"
#include "EngineRuntime.h"

#include "Log/Log.h"
#include "Core/Window.h"

#include "RHI/RHIBackendFactory.h"
#include "RHI/RHISwapchain.h"

#include "Platform/Vulkan/VulkanBackend.h"

namespace Catalpa {

    // 复用现有窗口接口，生成当前请求。最小化统一表达为零尺寸，初始化与后续更新都使用这个函数
    static RHISwapchainDesc MakeSwapchainDesc(const Window& window)
    {
        const auto [width, height] = window.GetFramebufferSize();

        return {
            .extent = window.IsMinimized() ? Extent2D{} : Extent2D{ width, height },
            .preferredPresentMode = window.IsVSync() ? RHIPresentMode::Fifo : RHIPresentMode::Immediate
        };
    }

    EngineRuntime::EngineRuntime() = default;

    EngineRuntime::~EngineRuntime() { Shutdown(); }

    RHIStatus EngineRuntime::Init(Window& window)
    {
        CAT_CORE_ASSERT(!m_Device, "Engine runtime already initialized!");

        RHIBackendFactory factory;
        factory.Register(GraphicsBackend::Vulkan, CreateVulkanDevice);

        RHIStatus status;
        m_Device = factory.Create(GraphicsBackend::Vulkan, window, status);
        if (!status.IsSuccess()) return status;

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        m_Swapchain = m_Device->CreateSwapchain(desc, status);

        if (!status.IsSuccess()) {
            Shutdown();
            return status;
        }

        m_LastSwapchainDesc = desc;
        return {};
    }

    void EngineRuntime::Shutdown()
    {
        // 释放 Runtime 持有的资源之前，先结束设备使用。
        if (m_Device) {
            // 等待 GPU → 重置帧命令并释放引用 → 注销交换链视图 → 销毁原生视图。
            const RHIStatus status = m_Device->WaitIdleAndDiscardFrameCommands();

            if (!status.IsSuccess() && status.code != RHIErrorCode::DeviceLost) {
                CAT_CORE_ASSERT(false, "Cannot safely release resources after device wait failed!");
                std::abort();
            }
        }

        m_Swapchain.reset();
        m_Device.reset();
        m_LastSwapchainDesc = {};
        m_SwapchainDirty = true;
    }

    // 根据事件标记、VSync 变化或交换链状态，决定是否需要查询窗口。
    // 查询后比较请求配置，确实需要时才重建。
    RHIStatus EngineRuntime::Update(Window& window)
    {
        const bool dirty = std::exchange(m_SwapchainDirty, false);
        const RHISwapchainState state = m_Swapchain->GetInfo().state;
        const RHIPresentMode requestedMode = window.IsVSync() ? RHIPresentMode::Fifo : RHIPresentMode::Immediate;

        const bool settingsChanged = requestedMode != m_LastSwapchainDesc.preferredPresentMode;
        const bool retrySuspended = state == RHISwapchainState::Suspended && !m_LastSwapchainDesc.extent.IsEmpty();

        if (!dirty && !settingsChanged && !retrySuspended && state != RHISwapchainState::OutOfDate) return {};

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        const bool restoring = state == RHISwapchainState::Suspended && !desc.extent.IsEmpty();

        if (desc == m_LastSwapchainDesc && !restoring && state != RHISwapchainState::OutOfDate) return {};

        if (const RHIStatus status = m_Device->WaitIdleAndDiscardFrameCommands(); !status.IsSuccess()) return status;

        const RHIStatus status = m_Swapchain->Recreate(desc);

        if (status.IsSuccess()) m_LastSwapchainDesc = desc;

        return status;
    }

}