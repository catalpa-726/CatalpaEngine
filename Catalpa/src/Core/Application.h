#pragma once

#include "Base.h"
#include "Log/Log.h"
#include "Window.h"
#include "Events/ApplicationEvent.h"


namespace Catalpa {

	class EngineRuntime;

	class CATALPA_API Application {
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }

	private:
		bool OnWindowClose(WindowCloseEvent& e);

	private:
		static Application* s_Instance;
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EngineRuntime> m_Runtime;

		bool m_Running = true;
	};

	Application* CreateApplication();
}