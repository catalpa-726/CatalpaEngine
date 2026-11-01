#include "catpch.h"
#include "Application.h"
#include "Log/Log.h"

#include "Runtime/EngineRuntime.h"

namespace Catalpa {

	Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        CAT_CORE_ASSERT(!s_Instance, "Application already exists!");

        m_Window = std::unique_ptr<Window>(Window::Create());
        m_Window->SetEventCallback(CAT_BIND_EVENT_FN(OnEvent));
        m_Runtime = std::make_unique<EngineRuntime>();

        s_Instance = this;
    }

    Application::~Application()
    {
        m_Runtime.reset();
        m_Window.reset();
        s_Instance = nullptr;
    }

	void Application::OnEvent(Event& e) {
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(CAT_BIND_EVENT_FN(Application::OnWindowClose));

		//for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); ) {
		//	(*--it)->OnEvent(e);
		//	if (e.Handled)
		//		break;
		//}
	}

    void Application::Run()
    {
        if (const RHIStatus status = m_Runtime->Init(*m_Window); !status.IsSuccess()) {
            CAT_CORE_ERROR("Engine initialization failed at {0}. RHI error: {1}", status.operation, static_cast<int>(status.code));
            return;
        }

        while (m_Running) {
            m_Window->OnUpdate();
        }
    }

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;
		return true;
	}

}
