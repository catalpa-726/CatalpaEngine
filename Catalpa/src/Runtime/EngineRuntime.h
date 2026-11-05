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
#include "Assets/AssetManager.h"
#include "Core/Jobs/JobSystem.h"
#include "ECS/ComponentRegistry.h"
#include "Renderer/RenderSurface.h"
#include "Renderer/Readback/Picking.h"
#include "Images/ImageAsset.h"

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
        // 宿主传绝对部署资源根；Runtime不从Box目录或源项目推导路径。
        [[nodiscard]] bool ConfigureResources(std::filesystem::path absoluteRoot, std::string& error);
        AssetManager& GetAssets() { return *m_Assets; }
        JobSystem& GetJobs() noexcept { return m_Jobs; }
        const ComponentRegistry* GetComponentSchemas() const noexcept { return m_ComponentSchemas.get(); }


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

        // 编辑宿主只借用场景句柄与表现快照，不能接管设备或缓存所有权。
        [[nodiscard]] bool SetEditorPreview(SceneHandle scene, uint64_t viewId, Extent2D extent, std::string& error);
        [[nodiscard]] bool GetEditorPreview(uint64_t viewId, RenderSurfaceSnapshot& surface) const;
        void CloseEditorPreview(uint64_t viewId);
        [[nodiscard]] bool SetEditorPreviewImage(uint64_t viewId, const AssetVersionRef<ImageAsset>& image,
            uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error);
        [[nodiscard]] bool RequestEditorPick(const PickingRequest& request, uint64_t& ticket, std::string& error);
        [[nodiscard]] bool PollEditorPick(uint64_t ticket, PickingResult& result);
        void CancelEditorPick(uint64_t ticket);
        [[nodiscard]] bool GetGpuFrameMilliseconds(double& milliseconds) const;
        [[nodiscard]] bool AddDebugBounds(const Aabb& bounds, const Math::Vec4& color, std::string& error, uint64_t surfaceId = 0);

    private:
        // 持有运行期对象；Shutdown 显式按 Renderer、交换链、设备的顺序释放。
        std::unique_ptr<RHIDevice> m_Device;
        std::unique_ptr<RHISwapchain> m_Swapchain;
        std::unique_ptr<Renderer> m_Renderer;
        SceneManager m_Scenes;
        JobSystem m_Jobs;
        std::unique_ptr<AssetManager> m_Assets;
        std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
        std::filesystem::path m_ResourceRoot;
        [[nodiscard]] bool InitAssets(std::string& error);

        SimulationClock m_Clock;
        InputActions m_Input;
        RendererProfile m_RendererProfile;
        FrameTime m_FrameTime;
        uint64_t m_FrameSequence = 0;
        uint64_t m_InputSceneIdentity = 0;
        bool m_SimulationStopped = false;

        // 由窗口表面变化事件标记重建请求。
        bool m_SwapchainDirty = true;
        struct PreviewRequest {
            SceneHandle scene;
            Extent2D extent;
            uint64_t generation = 1;
        };
        std::map<uint64_t, PreviewRequest> m_PreviewRequests;
        uint64_t m_NextPreviewGeneration = 1;
        std::string m_LastPreviewError;
        std::map<AssetId, std::string> m_EnvironmentRequestErrors;
        // 仅发非阻塞加载请求；固定版本由 Renderer 接包边界解析，不反向进入工具链。
        void RequestFrameAssets(const RenderFramePacket& packet);
        // 在 Layer UI 绘制前提取最新场景，但只由 Renderer 在真实提交后发布显示帧。
        [[nodiscard]] bool UpdateEditorPreviews(uint64_t sequence, std::string& error);

    private:
        // 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。
        RHISwapchainDesc m_LastSwapchainDesc{};

        // 只处理交换链更新，避免其中的早退跳过正常绘制。
        [[nodiscard]] RHIStatus UpdateSwapchain(Window& window);
        // 每个场景安装通用 Transform/提取阶段，不将客户端玩法移入引擎。
        [[nodiscard]] WorldStatus EnsureSceneSystems(Scene& scene);
    };

}
