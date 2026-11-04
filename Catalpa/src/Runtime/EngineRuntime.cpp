#include "catpch.h"
#include "EngineRuntime.h"

#include "Log/Log.h"
#include "Core/Window.h"

#include "Renderer/Renderer.h"

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

    RHIStatus EngineRuntime::Init(Window& window, const RHIBackendDesc& backendDesc)
    {
        CAT_CORE_ASSERT(!m_Device, "Engine runtime already initialized!");

        RHIBackendFactory factory;
        factory.Register(GraphicsBackend::Vulkan, CreateVulkanDevice);

        RHIStatus status;
        m_Device = factory.Create(backendDesc, window, status);
        if (!status.IsSuccess()) return status;

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        m_Swapchain = m_Device->CreateSwapchain(desc, status);

        if (!status.IsSuccess()) {
            Shutdown();
            return status;
        }

        m_Renderer = std::make_unique<Renderer>(*m_Device, *m_Swapchain);
        m_LastSwapchainDesc = desc;
        return {};
    }

    void EngineRuntime::Shutdown()
    {
        // 本步没有异步 World 消费者；先清场景及未提交 CPU 命令。
        if (const WorldStatus status = m_Scenes.Clear(); !status.IsSuccess()) {
            CAT_CORE_CRITICAL("Cannot shut down while scene data is borrowed: {0}", status.message);
            std::abort();
        }
        if (m_Device) {
            // 兼容路径采用设备空闲收尾；支持呈现 Fence 时追加独立的呈现等待。
            RHIStatus status = m_Device->WaitIdleAndDiscardFrameCommands();
            if (status.IsSuccess() && m_Swapchain && m_Device->GetCapabilities().presentFence) status = m_Swapchain->WaitForPresentCompletion();

            if (!status.IsSuccess() && status.code != RHIErrorCode::DeviceLost) {
                CAT_CORE_ASSERT(false, "Cannot safely release resources after GPU or presentation wait failed!");
                std::abort();
            }
        }

        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();
        m_LastSwapchainDesc = {};
        m_SwapchainDirty = true;
    }

    RHIStatus EngineRuntime::InitUI(std::span<const std::byte> fontPixels, Extent2D fontExtent)
    {
        if (!m_Renderer) return { RHIErrorCode::InvalidState, 0, "EngineRuntime::InitUI/Renderer" };
        return m_Renderer->InitUI(fontPixels, fontExtent);
    }

    WorldStatus EngineRuntime::UpdateScenes() { return m_Scenes.UpdateActive(); }

    // 每轮先根据事件标记、VSync 变化或交换链状态处理更新，再执行绘制。
    // UpdateSwapchain 负责查询和比较请求配置，确实需要时才重建。
    RHIStatus EngineRuntime::Update(Window& window, const ImGuiFrameData* uiFrame)
    {
        if (const RHIStatus status = UpdateSwapchain(window); !status.IsSuccess()) return status;
        return m_Renderer->Render(uiFrame);
    }

    RHIStatus EngineRuntime::UpdateSwapchain(Window& window)
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

        if (m_Device->GetCapabilities().presentFence) {
            if (const RHIStatus status = m_Swapchain->WaitForPresentCompletion(); !status.IsSuccess()) return status;
        }

        const RHIStatus status = m_Swapchain->Recreate(desc);

        if (status.IsSuccess()) m_LastSwapchainDesc = desc;

        return status;
    }

}
