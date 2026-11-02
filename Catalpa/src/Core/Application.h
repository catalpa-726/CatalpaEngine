#pragma once

#include "Base.h"
#include "Log/Log.h"
#include "Layer/LayerStack.h"
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

		void PushLayer(std::unique_ptr<Layer> layer);
		void PushOverlay(std::unique_ptr<Layer> overlay);

	private:
		struct PendingLayer {
			std::unique_ptr<Layer> layer;
			bool overlay = false;
		};

		void AttachPendingLayers();

		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowSurfaceChanged(Event& e);

	private:
		static Application* s_Instance;
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EngineRuntime> m_Runtime;

		LayerStack m_LayerStack;
		std::vector<PendingLayer> m_PendingLayers;

		bool m_Running = true;
	};

	Application* CreateApplication();
}