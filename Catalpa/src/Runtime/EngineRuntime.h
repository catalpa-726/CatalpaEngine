#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "RHI/RHIBackend.h"
#include <cstddef>
#include <span>

namespace Catalpa {

    class Window;
    class Renderer;
    class RHIDevice;
    class RHISwapchain;
    struct ImGuiFrameData;

    // 组装设备、交换链和 Renderer，衔接窗口状态与每帧渲染。
    class EngineRuntime {
    public:
        EngineRuntime();
        ~EngineRuntime();

        EngineRuntime(const EngineRuntime&) = delete;
        EngineRuntime& operator=(const EngineRuntime&) = delete;

        [[nodiscard]] RHIStatus Init(Window& window, const RHIBackendDesc& backendDesc);
        void Shutdown();

        // 每轮事件和 Layer 更新后，处理交换链变化并执行绘制。
        [[nodiscard]] RHIStatus Update(Window& window, const ImGuiFrameData* uiFrame = nullptr);

        // 初始化 UI 静态字体资源；不暴露设备或后端原生句柄给 Layer。
        [[nodiscard]] RHIStatus InitUI(std::span<const std::byte> fontPixels, Extent2D fontExtent);

        void RequestSwapchainUpdate() { m_SwapchainDirty = true; }

    private:
        // 持有运行期对象；Shutdown 显式按 Renderer、交换链、设备的顺序释放。
        std::unique_ptr<RHIDevice> m_Device;
        std::unique_ptr<RHISwapchain> m_Swapchain;
        std::unique_ptr<Renderer> m_Renderer;

        // 由窗口表面变化事件标记重建请求。
        bool m_SwapchainDirty = true;

    private:
        // 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。
        RHISwapchainDesc m_LastSwapchainDesc{};

        // 只处理交换链更新，避免其中的早退跳过正常绘制。
        [[nodiscard]] RHIStatus UpdateSwapchain(Window& window);
    };

}
