#include "catpch.h"
#include "Application.h"
#include "Log/Log.h"

#include "Runtime/EngineRuntime.h"
#include "RHI/RHIBackend.h"
#include "Layer/Imgui/ImGuiLayer.h"

namespace Catalpa {

    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        CAT_CORE_ASSERT(!s_Instance, "Application already exists!");

        m_Window = std::unique_ptr<Window>(Window::Create());
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
        m_Window.reset();
        s_Instance = nullptr;
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(CAT_BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowFramebufferResizeEvent>(CAT_BIND_EVENT_FN(Application::OnWindowSurfaceChanged));
        dispatcher.Dispatch<WindowMinimizeEvent>(CAT_BIND_EVENT_FN(Application::OnWindowSurfaceChanged));

        if (m_ImGuiLayer) m_ImGuiLayer->OnEvent(e);
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend() && !e.Handled; ++it) (*it)->OnEvent(e);
    }

    void Application::PushLayer(std::unique_ptr<Layer> layer) { m_PendingLayers.push_back({ .layer = std::move(layer), .overlay = false }); }

    void Application::PushOverlay(std::unique_ptr<Layer> overlay) { m_PendingLayers.push_back({ .layer = std::move(overlay), .overlay = true }); }

    SceneManager& Application::GetScenes() { return m_Runtime->GetScenes(); }

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
        // Debug 默认请求同步验证
        RHIBackendDesc backendDesc{};

#ifdef CATALPA_DEBUG
        backendDesc.diagnostics.validation = RHIValidationMode::Synchronization;
#endif

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

        AttachPendingLayers();

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (m_Running) {
            m_Window->OnUpdate();
            if (!m_Running) break;

            const auto now = Clock::now();
            const Deltatime timestep{ std::chrono::duration<float>(now - previous).count() };
            previous = now;

            for (Layer* layer : m_LayerStack) layer->OnUpdate(timestep);
            if (!m_Running) break;

            // Layer 本轮产生的场景命令在单一安全点提交，再生成 UI/渲染数据。
            if (const WorldStatus status = m_Runtime->UpdateScenes(); !status.IsSuccess()) {
                CAT_CORE_ERROR("Scene update failed: {0}", status.message);
                break;
            }

            m_ImGuiLayer->Begin();
            for (Layer* layer : m_LayerStack) layer->OnImGuiRender();
            if (const RHIStatus status = m_ImGuiLayer->End(); !status.IsSuccess()) {
                CAT_CORE_ERROR("UI frame failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
                break;
            }

            if (const RHIStatus status = m_Runtime->Update(*m_Window, &m_ImGuiLayer->GetFrameData()); !status.IsSuccess()) {
                CAT_CORE_ERROR("Engine update failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
                break;
            }

            AttachPendingLayers();
        }

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
        m_Runtime->RequestSwapchainUpdate();
        return false;
    }

}
