#pragma once

#include "Base.h"
#include "Log/Log.h"
#include "Layer/LayerStack.h"
#include "Window.h"
#include "Events/ApplicationEvent.h"

namespace Catalpa {

	class EngineRuntime;
	class ImGuiLayer;
	class SceneManager;
    class AssetManager;
    struct FrameTime;
    struct RenderFramePacket;

	class CATALPA_API Application {
	public:
		// 生命周期、主循环与事件入口。
		Application();
		virtual ~Application();

		void Run();

		// 请求主循环有序退出，仍由 Application 完成 Layer 与运行时清理。
        void RequestClose(int exitCode = 0) { if (exitCode != 0) m_ExitCode = exitCode; m_Running = false; }
        [[nodiscard]] int GetExitCode() const noexcept { return m_ExitCode; }

		void OnEvent(Event& e);

		// 应用与窗口访问。
		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }

		// 仅开放场景服务；设备、交换链和 Renderer 仍由 Runtime 管理。
		SceneManager& GetScenes();
        AssetManager& GetAssets(); // 仅Layer挂载后借用运行期服务，GPU仍封装在Runtime。
        // 仅借用最近完成的 CPU 时间/场景快照，供沙盒诊断；下一帧可能替换内容。
        [[nodiscard]] const FrameTime& GetFrameTime() const;
        [[nodiscard]] const RenderFramePacket& GetSceneFrame() const;

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
        int m_ExitCode = 0;
	};

	Application* CreateApplication();

	// 由唯一宿主 Box 处理启动参数，CPU 验证在创建窗口/设备前返回退出码。
	struct ApplicationStartupResult {
		bool handled = false;
		int exitCode = 0;
	};
	ApplicationStartupResult HandleApplicationArguments(int argc, char** argv);

}
