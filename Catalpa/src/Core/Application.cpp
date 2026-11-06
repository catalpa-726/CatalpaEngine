#include "catpch.h"
#include "Application.h"
#include "Log/Log.h"

#include "Runtime/EngineRuntime.h"
#include "RHI/RHIBackend.h"
#include "Layer/Imgui/ImGuiLayer.h"
#include "Core/Input.h"
#include "Core/PlatformServices.h"

namespace Catalpa {

    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        CAT_CORE_ASSERT(!s_Instance, "Application already exists!");

        m_Window = std::unique_ptr<Window>(Window::Create());
        Input::BindWindow(m_Window.get());
        m_Runtime = std::make_unique<EngineRuntime>();
        m_Window->SetEventCallback(CAT_BIND_EVENT_FN(OnEvent));

        s_Instance = this;
    }

    Application::~Application()
    {
        m_LayerStack.Clear();
        m_PendingLayers.clear();
        m_ImGuiLayer.reset();
        m_Runtime.reset();
        Input::BindWindow(nullptr);
        m_Window.reset();
        s_Instance = nullptr;
    }

    void Application::OnEvent(Event& e)
    {
        // 原始事件先进入收集器；玩法是否消费由本帧 UI 捕获与焦点统一决定。
        if (m_Runtime) m_Runtime->OnInputEvent(e);
        EventDispatcher dispatcher(e);
        if (e.GetEventType() == EventType::WindowClose) {
            // 正常关闭先交给宿主层完成文档确认；运行时错误仍可用非零退出码终止。
            for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend() && !e.Handled; ++it) (*it)->OnEvent(e);
            if (!e.Handled) dispatcher.Dispatch<WindowCloseEvent>(CAT_BIND_EVENT_FN(Application::OnWindowClose));
            return;
        }
        dispatcher.Dispatch<WindowFramebufferResizeEvent>(CAT_BIND_EVENT_FN(Application::OnWindowSurfaceChanged));
        dispatcher.Dispatch<WindowMinimizeEvent>(CAT_BIND_EVENT_FN(Application::OnWindowSurfaceChanged));

        if (m_ImGuiLayer) m_ImGuiLayer->OnEvent(e);
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend() && !e.Handled; ++it) (*it)->OnEvent(e);
    }

    void Application::RequestClose(int exitCode)
    {
        if (exitCode != 0) {
            m_ExitCode = exitCode;
            m_Running = false;
            return;
        }
        WindowCloseEvent request;
        OnEvent(request);
    }

    void Application::PushLayer(std::unique_ptr<Layer> layer) { m_PendingLayers.push_back({ .layer = std::move(layer), .overlay = false }); }

    void Application::PushOverlay(std::unique_ptr<Layer> overlay) { m_PendingLayers.push_back({ .layer = std::move(overlay), .overlay = true }); }

    AssetManager& Application::GetAssets() { return m_Runtime->GetAssets(); }
    JobSystem& Application::GetJobs() { return m_Runtime->GetJobs(); }
    SceneManager& Application::GetScenes() { return m_Runtime->GetScenes(); }
    const FrameTime& Application::GetFrameTime() const { return m_Runtime->GetFrameTime(); }
    const RenderFramePacket& Application::GetSceneFrame() const { return m_Runtime->GetSceneFrame(); }

    bool Application::SetEditorPreview(SceneHandle scene, uint64_t viewId, Extent2D extent, std::string& error)
    { return m_Runtime->SetEditorPreview(scene, viewId, extent, error); }
    bool Application::SetEditorCameraPreview(SceneHandle scene, uint64_t viewId, Extent2D extent,
        const Math::TransformTRS& pose, const CameraLens& lens, uint64_t revision, std::string& error)
    {
        return m_Runtime->SetEditorCameraPreview(scene, viewId, extent, pose, lens, revision, error);
    }

    void Application::SetEditorViewportActive(bool active)
    {
        m_Runtime->SetEditorViewportActive(active);
        m_EditorViewportActive = active;
        m_AllowEditorSystemKeys = false;
        m_AllowEditorPlayInput = false;
    }

    bool Application::SetEditorPlayScene(SceneHandle scene, bool paused, std::string& error)
    {
        return m_Runtime->SetPlayScene(scene, paused, error);
    }

    bool Application::StepEditorPlay(std::string& error) { return m_Runtime->StepPlay(error); }

    std::vector<BehaviorDebugSnapshot> Application::GetBehaviorDebug() const
    {
        return m_Runtime->GetBehaviorDebug();
    }
    bool Application::EmitBehaviorEvent(PersistentEntityId entity, std::string event, std::string& error) {
        return m_Runtime->EmitBehaviorEvent(entity, std::move(event), error);
    }

    bool Application::GetEditorPreview(uint64_t viewId, RenderSurfaceSnapshot& surface) const
    { return m_Runtime->GetEditorPreview(viewId, surface); }
    void Application::CloseEditorPreview(uint64_t viewId) { m_Runtime->CloseEditorPreview(viewId); }
    bool Application::SetEditorPreviewImage(uint64_t viewId, const AssetVersionRef<ImageAsset>& image,
        uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error)
    { return m_Runtime->SetEditorPreviewImage(viewId, image, mip, layer, channelMask, error); }
    bool Application::RequestEditorPick(const PickingRequest& request, uint64_t& ticket, std::string& error)
    { return m_Runtime->RequestEditorPick(request, ticket, error); }
    bool Application::PollEditorPick(uint64_t ticket, PickingResult& result) { return m_Runtime->PollEditorPick(ticket, result); }
    void Application::CancelEditorPick(uint64_t ticket) { m_Runtime->CancelEditorPick(ticket); }
    bool Application::GetGpuFrameMilliseconds(double& milliseconds) const { return m_Runtime->GetGpuFrameMilliseconds(milliseconds); }
    bool Application::AddDebugBounds(const Aabb& bounds, const Math::Vec4& color, std::string& error, uint64_t surfaceId)
    { return m_Runtime->AddDebugBounds(bounds, color, error, surfaceId); }

    void Application::AttachPendingLayers()
    {
        auto pending = std::exchange(m_PendingLayers, {});

        for (auto& entry : pending) {
            Layer* layer = entry.layer.get();

            if (entry.overlay) m_LayerStack.PushOverlay(layer);
            else m_LayerStack.PushLayer(layer);

            entry.layer.release()->OnAttach();
        }
    }

    void Application::Run()
    {
        // 所有提前返回默认为启动失败；初始化完整后才进入正常退出状态。
        m_ExitCode = 1;
        std::filesystem::path executableDirectory;
        std::string pathError;
        if (!GetExecutableDirectory(executableDirectory, pathError)) { CAT_CORE_ERROR("Cannot locate input profile: {0}", pathError); return; }
        std::ifstream profile(executableDirectory / "Resources" / "Config" / "InputProfiles.json", std::ios::binary);
        std::array<char, 65537> profileBytes{};
        if (!profile) { CAT_CORE_ERROR("Missing Resources/Config/InputProfiles.json; regenerate and build the current project."); return; }
        profile.read(profileBytes.data(), static_cast<std::streamsize>(profileBytes.size()));
        if (profile.bad() || profile.gcount() > 65536) { CAT_CORE_ERROR("Cannot read input profile or it exceeds 64 KiB."); return; }
        if (const auto input = m_Runtime->ConfigureInputProfile(std::string_view(profileBytes.data(), static_cast<size_t>(profile.gcount()))); !input.IsSuccess()) {
            CAT_CORE_ERROR("Invalid input profile: {0}", input.message);
            return;
        }
        // 与输入一样按可执行目录部署并受检读取渲染配置；失败不继续初始化GPU。
        std::ifstream rendererProfile(executableDirectory / "Resources" / "Config" / "RendererProfiles.json", std::ios::binary);
        if (!rendererProfile) { CAT_CORE_ERROR("Missing Resources/Config/RendererProfiles.json; regenerate and build the current project."); return; }
        rendererProfile.read(profileBytes.data(), static_cast<std::streamsize>(profileBytes.size()));
        if (rendererProfile.bad() || rendererProfile.gcount() > 65536) { CAT_CORE_ERROR("Cannot read renderer profile or it exceeds 64 KiB."); return; }
        if (const auto settings = m_Runtime->ConfigureRendererProfile(std::string_view(profileBytes.data(), static_cast<size_t>(rendererProfile.gcount()))); !settings.IsSuccess()) {
            CAT_CORE_ERROR("Invalid renderer profile at {0}. RHI error: {1}", settings.operation, static_cast<int>(settings.code));
            return;
        }
        // Debug 默认请求同步验证
        RHIBackendDesc backendDesc{};

#ifdef CATALPA_DEBUG
        backendDesc.diagnostics.validation = RHIValidationMode::Synchronization;
#endif

        if (!m_Runtime->ConfigureResources(executableDirectory / "Resources", pathError)) {
            CAT_CORE_ERROR("Cannot configure resources: {0}", pathError); return;
        }
        if (const RHIStatus status = m_Runtime->Init(*m_Window, backendDesc); !status.IsSuccess()) {
            CAT_CORE_ERROR("Engine initialization failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
            m_PendingLayers.clear();
            return;
        }

        m_ImGuiLayer = std::make_unique<ImGuiLayer>();
        m_ImGuiLayer->OnAttach();
        if (!m_ImGuiLayer->IsReady()) {
            m_ImGuiLayer.reset();
            m_PendingLayers.clear();
            return;
        }
        if (const RHIStatus status = m_Runtime->InitUI(m_ImGuiLayer->GetFontPixels(), m_ImGuiLayer->GetFontExtent()); !status.IsSuccess()) {
            CAT_CORE_ERROR("UI initialization failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
            m_ImGuiLayer.reset();
            m_PendingLayers.clear();
            return;
        }

        m_ExitCode = 0;
        AttachPendingLayers();

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (m_Running) {
            m_Window->OnUpdate();
            if (!m_Running) break;

            const auto now = Clock::now();
            const double realSeconds = std::chrono::duration<double>(now - previous).count();
            const Deltatime timestep{ static_cast<float>(realSeconds) };
            previous = now;

            // 当前帧平台事件已收集，先 NewFrame 取得本帧捕获，再运行玩法。
            m_AllowEditorSystemKeys = false;
            m_AllowEditorPlayInput = false;
            m_ImGuiLayer->Begin();
            for (Layer* layer : m_LayerStack) layer->OnUpdate(timestep);
            for (Layer* layer : m_LayerStack) layer->OnImGuiRender();
            auto capture = m_ImGuiLayer->GetInputCapture();
            if (m_AllowEditorSystemKeys) capture.keyboardCaptured = false;
            if (m_AllowEditorPlayInput) capture.keyboardCaptured = capture.mouseCaptured = false;
            capture.gameplayActive = !m_Window->IsMinimized() &&
                (!m_EditorViewportActive || m_AllowEditorPlayInput);
            if (m_Running) {
                const auto [width, height] = m_Window->GetFramebufferSize();
                if (const auto status = m_Runtime->UpdateSimulation(realSeconds, capture, width, height); !status.IsSuccess()) {
                    CAT_CORE_ERROR("Simulation/extraction failed: {0}", status.message);
                    m_ExitCode = 1;
                    m_Running = false;
                }
            }
            if (const RHIStatus status = m_ImGuiLayer->End(); !status.IsSuccess()) {
                CAT_CORE_ERROR("UI frame failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
                m_ExitCode = 1;
                break;
            }
            if (!m_Running) break;

            if (const RHIStatus status = m_Runtime->Update(*m_Window, &m_ImGuiLayer->GetFrameData()); !status.IsSuccess()) {
                CAT_CORE_ERROR("Engine update failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
                m_ExitCode = 1;
                break;
            }

            AttachPendingLayers();
        }

        m_Runtime->StopSimulation();
        m_LayerStack.Clear();
        m_PendingLayers.clear();
        m_ImGuiLayer.reset();
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowSurfaceChanged(Event&)
    {
        if (m_Runtime) m_Runtime->RequestSwapchainUpdate();
        return false;
    }

}
