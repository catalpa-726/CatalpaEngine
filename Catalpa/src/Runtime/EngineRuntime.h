#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"

namespace Catalpa {

    class Window;
    class Renderer;
    class RHIDevice;
    class RHISwapchain;

    // 用于隔离 Vulkan 底层
    class EngineRuntime {
    public:
        EngineRuntime();
        ~EngineRuntime();

        EngineRuntime(const EngineRuntime&) = delete;
        EngineRuntime& operator=(const EngineRuntime&) = delete;

        [[nodiscard]] RHIStatus Init(Window& window);
        void Shutdown();

        // 每轮事件和 Layer 更新后，处理交换链变化并执行绘制。
        [[nodiscard]] RHIStatus Update(Window& window);

        void RequestSwapchainUpdate() { m_SwapchainDirty = true; }

    private:
        std::unique_ptr<RHIDevice> m_Device;
        std::unique_ptr<RHISwapchain> m_Swapchain;
        std::unique_ptr<Renderer> m_Renderer;

        bool m_SwapchainDirty = true;

    private:
        // 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。
        RHISwapchainDesc m_LastSwapchainDesc{}; 

        // 只处理交换链更新，避免其中的早退跳过正常绘制。
        [[nodiscard]] RHIStatus UpdateSwapchain(Window& window);
    };

}