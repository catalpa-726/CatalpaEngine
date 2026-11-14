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
#include "Runtime/PackageSession.h"
#include "Core/Jobs/JobSystem.h"
#include "Runtime/Systems/AnimationSystem.h"
#include "Runtime/Systems/AudioExtractSystem.h"
#include "Runtime/Systems/UISystem.h"
#include "UI/UIInputRouter.h"
#include "Physics/PhysicsService.h"
#include "Navigation/NavigationService.h"
#include "Terrain/TerrainService.h"
#include "Runtime/Systems/ParticleSystem.h"
#include "ECS/ComponentRegistry.h"
#include "Renderer/RenderSurface.h"
#include "Renderer/Readback/Picking.h"
#include "Images/ImageAsset.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/BehaviorGraphRuntime.h"

namespace Catalpa {

	class Window;
	class Renderer;
	class RHIDevice;
	class RHISwapchain;
	struct ImGuiFrameData;
	class Event;
	class Scene;

	/* 组装唯一运行服务，拥有时钟/输入与场景集合；场景各自拥有系统调度器。 */
	class EngineRuntime {
	public:
		/**
		 * @brief 初始化 EngineRuntime。
		 */
		EngineRuntime();
		~EngineRuntime();

		EngineRuntime(const EngineRuntime&) = delete;
		EngineRuntime& operator=(const EngineRuntime&) = delete;

		/**
		 * @brief 以给定窗口和后端设置建立引擎服务、RHI 设备及渲染器。
		 *
		 * @param[in] window 承载呈现和输入的宿主窗口。
		 * @param[in] backendDesc 要启动的图形后端及诊断设置。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init(Window& window, const RHIBackendDesc& backendDesc);
		/**
		 * @brief 解除输入法目标并按依赖顺序收束仿真、资产和渲染资源。
		 */
		void Shutdown();
		/**
		 * @brief 启动资源包。
		 *
		 * @param[in] path 文件系统中的目标路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool StartPackage(const std::filesystem::path&, std::string& error);
		/**
		 * @brief 停止资源包。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool StopPackage(std::string& error);
		/**
		 * @brief 在设备和资产管理器初始化前设置部署资源目录。
		 * 宿主传绝对部署资源根；Runtime不从Box目录或源项目推导路径。
		 *
		 * @param[in] absoluteRoot 部署时提供的引擎资源根目录的绝对路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 传入绝对目录且仍允许配置时为 true，否则为 false。
		 */
		[[nodiscard]] bool ConfigureResources(std::filesystem::path absoluteRoot, std::string& error);
		AssetManager& GetAssets() { return *m_Assets; }
		[[nodiscard]] const std::filesystem::path& GetResourceRoot() const noexcept { return m_ResourceRoot; }
		JobSystem& GetJobs() noexcept { return m_Jobs; }
		AudioService& GetAudio() noexcept { return m_Audio; }
		/**
		 * @brief 设置编辑器游戏界面。
		 *
		 * @param[in] id 接收游戏界面绘制数据的编辑器视图标识。
		 * @param[in] data 供该视图合成的界面绘制快照。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool SetEditorGameUI(uint64_t, std::shared_ptr<const UIDrawData>, std::string& error);
		const ComponentRegistry* GetComponentSchemas() const noexcept { return m_ComponentSchemas.get(); }

		/**
		 * @brief 根据主窗口状态更新交换链并提交本帧的渲染工作。
		 * 事件和 Layer 更新后再比较交换链请求与当前配置，需要时重建，然后绘制。
		 *
		 * @param[in] window 决定交换链尺寸与呈现设置的主窗口。
		 * @param[in] uiFrame 可选的 ImGui 帧绘制数据。
		 *
		 * @return 交换链更新或绘制的 RHI 状态；前一步失败时直接返回其错误。
		 */
		[[nodiscard]] RHIStatus Update(Window& window, const ImGuiFrameData* uiFrame = nullptr);

		RHIStatus RenderPlatformWindow(uint32_t id, void* window, Extent2D extent, const ImGuiFrameData& frame);
		RHIStatus ReleasePlatformWindow(uint32_t id);
		/**
		 * @brief 初始化界面。
		 *
		 * @param[in] fontPixels 字体像素。
		 * @param[in] fontExtent 字体尺寸。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus InitUI(std::span<const std::byte> fontPixels, Extent2D fontExtent);

		void RequestSwapchainUpdate() { m_SwapchainDirty = true; }

		SceneManager& GetScenes() { return m_Scenes; }
		[[nodiscard]] WorldStatus UpdateScenes();
		[[nodiscard]] InputStatus ConfigureInputProfile(std::string_view json) { return m_Input.ParseProfile(json); }
		/**
		 * @brief 在渲染器初始化前解析并设置渲染配置。
		 * 渲染配置仅在设备初始化前解析并保存，不在飞行帧中热改资源契约。
		 *
		 * @param[in] json 待在设备初始化前加载的渲染配置 JSON 文本。
		 *
		 * @return 解析成功时返回正常状态；已初始化或配置无效时返回相应 RHI 状态。
		 */
		[[nodiscard]] RHIStatus ConfigureRendererProfile(std::string_view json);
		/**
		 * @brief 处理输入事件并更新对应的输入状态。
		 *
		 * @param[in] event 由窗口系统派发、交给运行时处理的输入事件。
		 *
		 * @return 游戏界面消费该事件时为 true，编辑器视口活动或界面未消费时为 false。
		 */
		bool OnInputEvent(const Event& event);
		bool RouteGameUIInput(const UIInputEvent& event, bool& consumed, std::string& error) {
			return m_GameUISystem.Input(event, consumed, error);
		}
		[[nodiscard]] TextInputSnapshot GetGameUITextInput() const { return m_GameUISystem.TextInput(); }
		/**
		 * @brief 推进展示帧和固定步长模拟，并为主窗口和编辑预览提取当前场景。
		 *
		 * @param[in] realSeconds 上一展示帧到当前帧经过的秒数，须为有限非负值。
		 * @param[in] capture 宿主报告的输入捕获状态，与当前游戏界面焦点及运行模式一起决定本帧玩法输入。
		 * @param[in] width 主帧目标的像素宽度，供运行时视图和游戏界面确定显示区域。
		 * @param[in] height 主帧目标的像素高度，与宽度共同确定本轮显示范围。
		 *
		 * @return 本轮更新完成时返回正常状态；时钟、场景系统或提取无法继续时返回相应世界状态。
		 */
		[[nodiscard]] WorldStatus UpdateSimulation(
		    double realSeconds, InputCapture capture, uint32_t width, uint32_t height);
		/**
		 * @brief 停止模拟。
		 */
		void StopSimulation() noexcept;
		/**
		 * @brief 轮询脚本场景切换请求，准备仍属于当前请求的候选场景。
		 * 返回true才转移候选Scene的所有权；false+空error表示无请求或仍在异步加载。
		 *
		 * @param[out] ownedCandidate 每次调用先清空，成功时接收已实例化的候选场景句柄。
		 * @param[out] error 接收资源加载或场景实例化失败的原因。没有请求、请求尚未就绪或已过期时，也可返回 false 且不填诊断。
		 *
		 * @return 成功交付候选句柄时为 true。无请求、加载未完成、请求过期或处理失败时为 false。
		 */
		bool PollScriptSceneSwitch(SceneHandle& ownedCandidate, std::string& error);
		[[nodiscard]] const FrameTime& GetFrameTime() const noexcept { return m_FrameTime; }
		[[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept;

		[[nodiscard]] bool SetEditorPreview(SceneHandle scene, uint64_t viewId, Extent2D extent, std::string& error);
		/**
		 * @brief 设置编辑器相机预览。
		 *
		 * @param[in] scene 提供预览实体与渲染数据的场景句柄。
		 * @param[in] viewId 目标视图的标识。
		 * @param[in] extent 目标区域的尺寸。
		 * @param[in] cameraPose 编辑器预览相机的世界空间位置和朝向。
		 * @param[in] lens 生成预览投影矩阵所需的镜头参数。
		 * @param[in] historyRevision 用于区分历史数据的修订号。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] display 指定编辑器视图显示方式的选项。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool SetEditorCameraPreview(SceneHandle scene, uint64_t viewId, Extent2D extent,
		    const Math::TransformTRS& cameraPose, const CameraLens& lens, uint64_t historyRevision, std::string& error,
		    EditorViewDisplay display = {});
		/**
		 * @brief 设置编辑器视口活动。
		 *
		 * @param[in] active 指示编辑器视口当前是否处于活动状态。
		 */
		void SetEditorViewportActive(bool active);
		/**
		 * @brief 设置运行模式场景。
		 *
		 * @param[in] scene 用于运行游戏模拟的场景句柄。
		 * @param[in] paused 是否暂停。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool SetPlayScene(SceneHandle scene, bool paused, std::string& error);
		/**
		 * @brief 请求暂停中的 Play 会话只推进一个固定步。
		 *
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool StepPlay(std::string& error);
		[[nodiscard]] std::vector<BehaviorDebugSnapshot> GetBehaviorDebug() const { return m_Behaviors.Snapshot(); }
		[[nodiscard]] bool EmitBehaviorEvent(PersistentEntityId entity, std::string event, std::string& error) {
			return m_Behaviors.Emit(entity, std::move(event), error);
		}
		[[nodiscard]] bool GetEditorPreview(uint64_t viewId, RenderSurfaceSnapshot& surface) const;
		/**
		 * @brief 关闭编辑器预览。
		 *
		 * @param[in] viewId 目标视图的标识。
		 */
		void CloseEditorPreview(uint64_t viewId);
		/**
		 * @brief 保留最近发布的编辑器预览图像。
		 *
		 * @param[in] viewId 目标视图的标识。
		 */
		void FreezeEditorPreview(uint64_t viewId);
		/**
		 * @brief 设置编辑器预览图像。
		 *
		 * @param[in] viewId 目标视图的标识。
		 * @param[in] image 供编辑器预览读取的固定版本图像。
		 * @param[in] mip 纹理的 mip 层级。
		 * @param[in] layer 指定预览内容所在的纹理数组层索引。
		 * @param[in] channelMask 选择图像通道的位掩码。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool SetEditorPreviewImage(uint64_t viewId, const AssetVersionRef<ImageAsset>& image,
		    uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error);
		/**
		 * @brief 请求编辑器拾取。
		 *
		 * @param[in] request 描述本次请求的 PickingRequest。
		 * @param[out] ticket 成功时接收供后续轮询或取消拾取请求的票据。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool RequestEditorPick(const PickingRequest& request, uint64_t& ticket, std::string& error);
		[[nodiscard]] bool PollEditorPick(uint64_t ticket, PickingResult& result);
		void CancelEditorPick(uint64_t ticket);
		[[nodiscard]] bool GetPlatformWindowPresentation(uint32_t id, PlatformWindowPresentation& output) const;
		/**
		 * @brief 返回 GPU 帧毫秒。
		 *
		 * @param[out] milliseconds 接收 GPU 帧耗时，当前无可用计时数据时写入零。
		 *
		 * @return 渲染器已取得 GPU 计时结果时为 true，否则为 false。
		 */
		[[nodiscard]] bool GetGpuFrameMilliseconds(double& milliseconds) const;
		/**
		 * @brief 返回受管理的 GPU 有效载荷字节。
		 *
		 * @param[out] used 接收当前计入管理的 GPU 载荷字节数。
		 * @param[out] budget 接收受管理 GPU 载荷的预算上限，单位为字节。
		 *
		 * @return 渲染器提供非零预算时为 true，否则为 false。
		 */
		[[nodiscard]] bool GetManagedGpuPayloadBytes(uint64_t& used, uint64_t& budget) const;
		/**
		 * @brief 添加调试包围盒。
		 *
		 * @param[in] bounds 调试绘制所显示的轴对齐包围盒。
		 * @param[in] color 用于画出包围盒的调试颜色。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] surfaceId 预览渲染表面的标识。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool AddDebugBounds(
		    const Aabb& bounds, const Math::Vec4& color, std::string& error, uint64_t surfaceId = 0);

	private:
		/* 持有运行期对象；Shutdown 显式按 Renderer、交换链、设备的顺序释放。 */
		std::unique_ptr<RHIDevice> m_Device;
		Window* m_Window = nullptr; /* 借用宿主窗口，生命周期覆盖 Runtime。 */
		std::unique_ptr<RHISwapchain> m_Swapchain;
		std::unique_ptr<Renderer> m_Renderer;
		/* 场景由运行时持有，宿主使用服务引用或带代次的句柄访问。 */
		SceneManager m_Scenes;
		JobSystem m_Jobs;
		std::unique_ptr<AssetManager> m_Assets;
		std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
		std::filesystem::path m_ResourceRoot;
		[[nodiscard]] bool InitAssets(std::string& error);

		SimulationClock m_Clock;
		AnimationSystem m_Animation;
		ParticleSystem m_Particles;
		AssetVersionRef<MeshAsset> m_ParticleQuad;
		TerrainService m_Terrain;
		std::map<uint64_t, std::unique_ptr<TerrainService>> m_TerrainPreviews;
		PhysicsService m_Physics;
		NavigationService m_Navigation;
		AudioService m_Audio;
		AudioExtractSystem m_AudioExtract;
		UISystem m_GameUISystem;
		UIInputRouter m_GameUIInputRouter;
		ScriptRuntime m_Scripts;
		std::optional<ScriptRuntime::SceneRequest> m_PendingScriptScene;
		SceneHandle m_StandaloneScriptScene;
		SceneHandle m_PrePackageScriptScene;
		bool m_PackageStopping = false;
		void ForwardDynamicEvents(Scene&);
		BehaviorGraphRuntime m_Behaviors;
		SceneHandle m_PlayScene;
		bool m_PlayPaused = false;
		std::string m_LastScriptDiagnostic;
		std::map<PersistentEntityId, std::string> m_BehaviorDiagnostics;
		InputActions m_Input;
		RendererProfile m_RendererProfile;
		FrameTime m_FrameTime;
		uint64_t m_FrameSequence = 0;
		uint64_t m_InputSceneIdentity = 0;
		bool m_SimulationStopped = false;
		bool m_EditorViewportActive = false;
		bool m_StandalonePaused = false;
		double m_EditorPreviewSeconds = 0;
		double m_UISeconds = 0;
		void AppendAnimationFrame(const Scene&, RenderFramePacket&);
		bool AppendDynamicFrame(Scene&, RenderFramePacket&, bool preview, std::string&);
		std::string m_LastSceneCameraError;

		/* 由窗口表面变化事件标记重建请求。 */
		bool m_SwapchainDirty = true;
		struct PreviewRequest {
			SceneHandle scene;
			Extent2D extent;
			uint64_t generation = 1;
			std::optional<RenderView> independentView;
		};
		std::map<uint64_t, PreviewRequest> m_PreviewRequests;
		uint64_t m_NextPreviewGeneration = 1;
		std::string m_LastPreviewError;
		std::map<AssetId, std::string> m_EnvironmentRequestErrors;
		void RequestFrameAssets(const RenderFramePacket& packet);
		[[nodiscard]] bool StorePreviewRequest(SceneHandle scene, uint64_t viewId, Extent2D extent,
		    std::optional<RenderView> independentView, std::string& error);
		[[nodiscard]] bool UpdateEditorPreviews(uint64_t sequence, bool primaryFrameFresh, std::string& error);

	private:
		PackageSession m_PackageSession;

		/* 缓存上次接受的请求配置。后端可能限制尺寸或回退呈现模式，因此不能用实际结果代替这个缓存，否则容易每轮都重建。 */
		RHISwapchainDesc m_LastSwapchainDesc{};

		[[nodiscard]] RHIStatus UpdateSwapchain(Window& window);
		[[nodiscard]] WorldStatus EnsureSceneSystems(Scene& scene);
	};

}
