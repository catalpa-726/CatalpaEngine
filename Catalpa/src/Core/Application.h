#pragma once

#include "Base.h"
#include "Log/Log.h"
#include "Layer/LayerStack.h"
#include "Window.h"
#include "Events/ApplicationEvent.h"

namespace Catalpa {

	class EngineRuntime;
	class ImGuiLayer;

	class CATALPA_API Application {
	public:
		// 生命周期、主循环与事件入口。
		Application();
		virtual ~Application();

		void Run();

		// 请求主循环有序退出，仍由 Application 完成 Layer 与运行时清理。
		void RequestClose() { m_Running = false; }

		void OnEvent(Event& e);

		// 应用与窗口访问。
		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }

		// 先暂存所有权，初始化成功或当前帧结束后再挂载。
		void PushLayer(std::unique_ptr<Layer> layer);
		void PushOverlay(std::unique_ptr<Layer> overlay);

	private:
		struct PendingLayer {
			std::unique_ptr<Layer> layer;
			bool overlay = false;
		};

		// Layer 挂载与应用内部事件处理。
		void AttachPendingLayers();

		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowSurfaceChanged(Event& e);

	private:
		// 应用身份与运行期对象。
		static Application* s_Instance;
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EngineRuntime> m_Runtime;
		// UI 上下文先于业务 Layer 创建，晚于业务 Layer 卸载。
		std::unique_ptr<ImGuiLayer> m_ImGuiLayer;

		// 已挂载的层与待挂载的所有权队列。
		LayerStack m_LayerStack;
		std::vector<PendingLayer> m_PendingLayers;

		bool m_Running = true;
	};

	Application* CreateApplication();

}
