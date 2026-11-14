#pragma once

#include "Base.h"
#include "Log/Log.h"
#include "Layer/LayerStack.h"
#include "Window.h"
#include "Events/ApplicationEvent.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Renderer/RenderSurface.h"
#include "Renderer/Readback/Picking.h"
#include "Images/ImageAsset.h"
#include "Assets/AssetHandle.h"
#include "Geometry/Bounds.h"
#include "Scripting/BehaviorDebugSnapshot.h"

namespace Catalpa {
	class AudioService;
	struct UIDrawData;
	struct UIInputEvent;

	class EngineRuntime;
	class ImGuiLayer;
	class SceneManager;
	class AssetManager;
	class JobSystem;
	struct FrameTime;
	struct RenderFramePacket;

	class CATALPA_API Application {
	public:
		/**
		 * @brief 创建主窗口、绑定输入和事件回调，并初始化引擎运行时。
		 * 生命周期、主循环与事件入口。
		 */
		Application();
		/**
		 * @brief 结束 Application 对象的生命周期。
		 */
		virtual ~Application();

		/**
		 * @brief 初始化运行时并驱动应用主循环，直到收到关闭请求。
		 */
		void Run();

		/**
		 * @brief 记录退出码并请求主循环有序退出。
		 * 请求主循环有序退出，仍由 Application 完成 Layer 与运行时清理。
		 *
		 * @param[in] exitCode 向系统返回的进程退出码。
		 */
		void RequestClose(int exitCode = 0);
		[[nodiscard]] int GetExitCode() const noexcept { return m_ExitCode; }

		/**
		 * @brief 处理传递到此层的事件。
		 *
		 * @param[in] e 本次分派的事件事件。
		 */
		void OnEvent(Event& e);

		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }

		SceneManager& GetScenes();
		AssetManager& GetAssets(); /* 仅Layer挂载后借用运行期服务，GPU仍封装在Runtime。 */
		[[nodiscard]] const std::filesystem::path& GetResourceRoot() const;
		bool StartPackage(const std::filesystem::path&, std::string& error);
		bool StopPackage(std::string& error);
		AudioService& GetAudio();
		bool PollScriptSceneSwitch(SceneHandle& ownedCandidate, std::string& error);
		bool RouteGameUIInput(const UIInputEvent&, bool& consumed, std::string& error);
		[[nodiscard]] TextInputSnapshot GetGameUITextInput() const;
		[[nodiscard]] bool SetEditorGameUI(uint64_t, std::shared_ptr<const UIDrawData>, std::string& error);
		JobSystem& GetJobs(); /* 借用Runtime唯一CPU调度器；Layer只能取消/等待自己提交的任务。 */
		/**
		 * @brief 借用运行时最近发布的帧时间。
		 * 仅借用最近完成的 CPU 时间/场景快照，供沙盒诊断；下一帧可能替换内容。
		 *
		 * @return 由运行时持有的帧时间引用，随后帧更新时内容可能变化。
		 */
		[[nodiscard]] const FrameTime& GetFrameTime() const;
		[[nodiscard]] const RenderFramePacket& GetSceneFrame() const;

		[[nodiscard]] bool SetEditorPreview(SceneHandle scene, uint64_t viewId, Extent2D extent, std::string& error);
		[[nodiscard]] bool SetEditorCameraPreview(SceneHandle scene, uint64_t viewId, Extent2D extent,
		    const Math::TransformTRS& cameraPose, const CameraLens& lens, uint64_t historyRevision, std::string& error,
		    EditorViewDisplay display = {});
		/**
		 * @brief 切换由编辑器视口还是主窗口呈现场景。
		 * 挂载工作区时由视口呈现场景，卸载后恢复宿主主窗口的场景呈现。
		 *
		 * @param[in] active 为 true 时由编辑视口呈现场景，为 false 时恢复主窗口呈现。
		 */
		void SetEditorViewportActive(bool active);
		[[nodiscard]] bool SetEditorPlayScene(SceneHandle scene, bool paused, std::string& error);
		[[nodiscard]] bool StepEditorPlay(std::string& error);
		[[nodiscard]] std::vector<BehaviorDebugSnapshot> GetBehaviorDebug() const;
		[[nodiscard]] bool EmitBehaviorEvent(PersistentEntityId entity, std::string event, std::string& error);
		void AllowEditorViewportSystemKeys() noexcept { m_AllowEditorSystemKeys = true; }
		void AllowEditorPlayInput() noexcept { m_AllowEditorPlayInput = true; }
		[[nodiscard]] bool GetEditorPreview(uint64_t viewId, RenderSurfaceSnapshot& surface) const;
		void CloseEditorPreview(uint64_t viewId);
		void FreezeEditorPreview(uint64_t viewId); /* 保留已发布图像，停止提取和渲染缓存缩略图。 */
		[[nodiscard]] bool SetEditorPreviewImage(uint64_t viewId, const AssetVersionRef<ImageAsset>& image,
		    uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error);
		[[nodiscard]] bool RequestEditorPick(const PickingRequest& request, uint64_t& ticket, std::string& error);
		[[nodiscard]] bool PollEditorPick(uint64_t ticket, PickingResult& result);
		void CancelEditorPick(uint64_t ticket);
		[[nodiscard]] bool GetPlatformWindowPresentation(uint32_t id, PlatformWindowPresentation& output) const;
		[[nodiscard]] bool GetGpuFrameMilliseconds(double& milliseconds) const;
		[[nodiscard]] bool GetManagedGpuPayloadBytes(uint64_t& used, uint64_t& budget) const;
		[[nodiscard]] bool AddDebugBounds(
		    const Aabb& bounds, const Math::Vec4& color, std::string& error, uint64_t surfaceId = 0);

		/**
		 * @brief 接管待挂载层，在初始化完成或当前帧结束后执行挂载。
		 * 先暂存所有权，初始化成功或当前帧结束后再挂载。
		 *
		 * @param[in] layer 将所有权移交给应用、等待挂载的普通层。
		 */
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
		/* 应用身份与运行期对象。 */
		static Application* s_Instance;
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<EngineRuntime> m_Runtime;
		/* UI 上下文先于业务 Layer 创建，晚于业务 Layer 卸载。 */
		std::unique_ptr<ImGuiLayer> m_ImGuiLayer;

		/* 已挂载的层与待挂载的所有权队列。 */
		LayerStack m_LayerStack;
		std::vector<PendingLayer> m_PendingLayers;

		bool m_Running = true;
		int m_ExitCode = 0;
		/* 仅当本帧的编辑视口接收导航输入时放行系统按键。 */
		bool m_AllowEditorSystemKeys = false;
		bool m_AllowEditorPlayInput = false;
		bool m_EditorViewportActive = false;
	};

	Application* CreateApplication();

	/* 由唯一宿主 Box 处理启动参数，CPU 验证在创建窗口/设备前返回退出码。 */
	struct ApplicationStartupResult {
		bool handled = false;
		int exitCode = 0;
	};
	/**
	 * @brief 在创建窗口之前分派工具命令与普通启动参数。
	 * 工具参数在 Core/Entry 调用 CreateApplication 前处理；普通启动路径保持唯一 Box。
	 * 第 1、2 个形参来自进程入口；此实现直接读取系统命令行，忽略这两个值。
	 *
	 * @param[in] argc 启动入口传入的参数个数；此实现使用平台参数读取接口。
	 * @param[in] argv 启动入口传入的参数数组；此实现使用平台参数读取接口。
	 *
	 * @return 指示是否已处理命令及进程退出码；无法读取进程参数时结果为已处理、退出码 2。
	 */
	ApplicationStartupResult HandleApplicationArguments(int argc, char** argv);

}
