#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"

namespace Catalpa {

    class Window;
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

        // 每轮窗口事件处理后，判断是否需要重建交换链。
        [[nodiscard]] RHIStatus Update(Window& window);

        void RequestSwapchainUpdate() { m_SwapchainDirty = true; }

    private:
        std::unique_ptr<RHIDevice> m_Device;
        std::unique_ptr<RHISwapchain> m_Swapchain;

        bool m_SwapchainDirty = true;

        // 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。
        RHISwapchainDesc m_LastSwapchainDesc{}; 
    };

}