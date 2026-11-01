#include "catpch.h"
#include "Application.h"
#include "Log/Log.h"

namespace Catalpa {

	Application* Application::s_Instance = nullptr;

	Application::Application()
	{
		CAT_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(CAT_BIND_EVENT_FN(OnEvent));
	}

	Application::~Application()
	{

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
		while (m_Running) {

			m_Window->OnUpdate();

		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;
		return true;
	}

}
