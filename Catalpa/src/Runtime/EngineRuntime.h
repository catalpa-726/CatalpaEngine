#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "RHI/RHIBackend.h"
#include "Scene/SceneManager.h"
#include "Core/Time/SimulationClock.h"
#include "Runtime/Input/InputActions.h"
#include "Renderer/RenderFramePacket.h"
#include "Renderer/RendererFrameContext.h"

namespace Catalpa {

    class Window;
    class Renderer;
    class RHIDevice;
    class RHISwapchain;
    struct ImGuiFrameData;
    class Event;
    class Scene;

    // 组装唯一运行服务，拥有时钟/输入与场景集合；场景各自拥有系统调度器。
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

        // 场景独占于 Runtime，Box/Layer 只借用服务和代次句柄。
        SceneManager& GetScenes() { return m_Scenes; }
        [[nodiscard]] WorldStatus UpdateScenes();
        // 平台先收集事件，UI NewFrame 后提交捕获策略，再运行固定步及派生提取。
        [[nodiscard]] InputStatus ConfigureInputProfile(std::string_view json) { return m_Input.ParseProfile(json); }
        // 渲染配置仅在设备初始化前解析并保存，不在飞行帧中热改资源契约。
        [[nodiscard]] RHIStatus ConfigureRendererProfile(std::string_view json);
        void OnInputEvent(const Event& event) { m_Input.OnEvent(event); }
        [[nodiscard]] WorldStatus UpdateSimulation(double realSeconds, InputCapture capture, uint32_t width, uint32_t height);
        void StopSimulation() noexcept;
        [[nodiscard]] const FrameTime& GetFrameTime() const noexcept { return m_FrameTime; }
        [[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept;

    private:
        // 持有运行期对象；Shutdown 显式按 Renderer、交换链、设备的顺序释放。
        std::unique_ptr<RHIDevice> m_Device;
        std::unique_ptr<RHISwapchain> m_Swapchain;
        std::unique_ptr<Renderer> m_Renderer;
        SceneManager m_Scenes;
        SimulationClock m_Clock;
        InputActions m_Input;
        RendererProfile m_RendererProfile;
        FrameTime m_FrameTime;
        uint64_t m_FrameSequence = 0;
        uint64_t m_InputSceneIdentity = 0;
        bool m_SimulationStopped = false;

        // 由窗口表面变化事件标记重建请求。
        bool m_SwapchainDirty = true;

    private:
        // 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。
        RHISwapchainDesc m_LastSwapchainDesc{};

        // 只处理交换链更新，避免其中的早退跳过正常绘制。
        [[nodiscard]] RHIStatus UpdateSwapchain(Window& window);
        // 每个场景安装通用 Transform/提取阶段，不将客户端玩法移入引擎。
        [[nodiscard]] WorldStatus EnsureSceneSystems(Scene& scene);
    };

}
