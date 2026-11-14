#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "Renderer/UI/ImGuiRenderer.h"
#include "Renderer/RenderScene.h"
#include "Renderer/RenderSurface.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/MaterialGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "Renderer/Readback/ReadbackManager.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/Resources/ComputePipelineLibrary.h"
#include "Renderer/Resources/ResourceResidencyManager.h"
#include "Renderer/Passes/GameUIPass.h"

namespace Catalpa {
	class RHIDevice;
	class RHISwapchain;
	class RenderGraph;
	class AssetManager;
	class RHIQueryPool;
	class JobSystem;
	struct RGIndexedDrawDesc;

	/* 宿主显式选择主窗口是否展示场景；离屏surface的渲染不受此选项影响。 */
	enum class MainScenePresentation : uint8_t { MainWindow, OffscreenOnly };

	struct RendererStatistics {
		bool timestampSupported = false;
		bool gpuDurationAvailable = false;
		double gpuMilliseconds = 0;
		uint64_t submittedSequence = 0;
		uint32_t previewCount = 0;
		uint64_t managedPayloadBytes = 0;
		uint64_t managedPayloadBudget = 0;
		uint64_t deviceAllocationBytes = 0;
		uint64_t deviceBlockBytes = 0;
		uint64_t heapUsageBytes = 0;
		uint64_t heapBudgetBytes = 0;
		bool driverMemoryBudget = false;
	};

	/* 拥有表现资源与缓存；所有世界/编辑行为仍通过自有值快照进入。 */
	class Renderer {
	public:
		Renderer(RHIDevice&, RHISwapchain&, AssetManager&, const RendererProfile& profile = {});
		/**
		 * @brief 结束 Renderer 对象的生命周期。
		 */
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		/**
		 * @brief 准备帧槽和预览、构建渲染图，并提交本帧的渲染与呈现。
		 *
		 * @param[in] uiFrame 当前帧的 ImGui 绘制数据，可为空；为空时仍可渲染场景和游戏界面。
		 *
		 * @return 返回本次准备、录制、提交或呈现状态；帧槽暂不可复用或未取得交换链图像时可能成功跳过本帧。
		 */
		[[nodiscard]] RHIStatus Render(const ImGuiFrameData* uiFrame = nullptr);
		void SetJobs(JobSystem* jobs) noexcept { m_Jobs = jobs; }
		/**
		 * @brief 解析场景环境资产的固定版本，并验证后发布新的主场景帧。
		 *
		 * @param[in] packet 主场景新帧的视图、实例、光源和环境快照，序号须晚于此前接受的帧。
		 *
		 * @return 返回环境解析及帧校验状态；无效帧不会取代此前成功发布的快照。
		 */
		[[nodiscard]] Math::MathStatus SetSceneFrame(RenderFramePacket packet);
		[[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept { return m_Scene.GetFrame(); }
		void ClearSceneFrame() noexcept;
		void SetGameUI(std::shared_ptr<const UIDrawData> data) { m_GameUIDraw = std::move(data); }
		/**
		 * @brief 在已存在的预览表面绑定游戏界面绘制数据。
		 *
		 * @param[in] id 已创建的场景预览表面标识。
		 * @param[in] data 由预览持有的只读界面绘制快照；传空指针解除该表面的游戏界面。
		 * @param[out] error 找不到预览表面时输出原因，绑定成功时清空。
		 *
		 * @return 预览表面存在并安装数据时返回 true，找不到该表面时返回 false。
		 */
		[[nodiscard]] bool SetPreviewGameUI(uint64_t id, std::shared_ptr<const UIDrawData>, std::string& error);
		/**
		 * @brief 默认保留主窗口场景；编辑宿主可只在中央离屏surface展示，交换链仍清底色并合成UI。
		 *
		 * @param[in] presentation 宿主的主场景呈现模式。
		 */
		void SetMainScenePresentation(MainScenePresentation) noexcept;
		[[nodiscard]] MainScenePresentation GetMainScenePresentation() const noexcept {
			return m_MainScenePresentation;
		}
		/**
		 * @brief 在分离的编辑器原生窗口上创建或复用交换链，绘制并呈现该窗口的界面。
		 *
		 * @param[in] id ImGui 平台窗口的标识，用于查找该窗口的专属交换链。
		 * @param[in] window 与 id 对应的原生窗口指针，用于首次创建呈现表面。
		 * @param[in] extent 该窗口帧缓冲的当前物理像素宽高。
		 * @param[in] frame 需绘制到该窗口的界面几何、裁剪区域与纹理标记。
		 *
		 * @return 返回独立窗口的绘制及呈现状态；主帧未就绪或目标尺寸为空时成功跳过。
		 */
		RHIStatus RenderPlatformWindow(uint32_t id, void* window, Extent2D extent, const ImGuiFrameData& frame);
		/**
		 * @brief 取得指定编辑器窗口最近的呈现记录。
		 *
		 * @param[in] id 要查询的平台窗口标识。
		 * @param[out] output 接收实际尺寸和已呈现帧的统计。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		bool GetPlatformWindowPresentation(uint32_t id, PlatformWindowPresentation& output) const;
		/**
		 * @brief 结束平台窗口的后端呈现并释放其交换链。
		 *
		 * @param[in] id ImGui 平台视口标识。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus ReleasePlatformWindow(uint32_t id);
		/**
		 * @brief 将界面字体图集发布为内置图像资产，并准备其 GPU 纹理。
		 *
		 * @param[in] pixels 从首行起紧密排列的 RGBA8 图集字节，长度必须等于宽乘高乘 4。
		 * @param[in] extent 字体图集的像素宽高，两者均须大于零。
		 *
		 * @return 返回图集尺寸和像素校验、内置资产发布及 GPU 上传准备的结果。
		 */
		[[nodiscard]] RHIStatus InitUI(std::span<const std::byte> pixels, Extent2D extent);

		/**
		 * @brief 每个surface独占表现目标与帧槽，CPU/GPU资产缓存仍由同一个Renderer共享。
		 *
		 * @param[in] surfaceId 必须与视图 ID 一致的非零标识。
		 * @param[in] packet 包含唯一预览视图的帧快照。
		 * @param[out] error 失败时接收原因。
		 *
		 * @return 预览表面接收帧快照时返回 true，失败原因写入 error。
		 */
		[[nodiscard]] bool SetPreviewFrame(uint64_t surfaceId, RenderFramePacket packet, std::string& error);
		/**
		 * @brief 查询供编辑器采样和拾取的已发布离屏画面。
		 *
		 * @param[in] surfaceId 待查询的预览标识。
		 * @param[out] output 接收画面版本、尺寸和纹理标识等快照。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		[[nodiscard]] bool GetPreviewSurface(uint64_t surfaceId, RenderSurfaceSnapshot& output) const;
		/**
		 * @brief 释放预览目标并取消该表面尚未返回的拾取。
		 *
		 * @param[in] surfaceId 要释放的离屏预览标识。
		 */
		void ReleasePreviewSurface(uint64_t surfaceId);
		/**
		 * @brief 停止该预览继续绘制场景，并保留已发布的可采样画面。
		 *
		 * @param[in] surfaceId 已发布画面的预览标识。
		 */
		void FreezePreviewSurface(uint64_t surfaceId);
		/**
		 * @brief 为图像版本的指定细节层级和颜色通道登记预览，并替换该表面的展示资源。
		 *
		 * @param[in] surfaceId 非零预览表面标识，可对应已有表面或本次创建的表面。
		 * @param[in] image 要展示的固定版本二维图像；当前仅支持单一数组层。
		 * @param[in] mip 从 0 开始的细节层级，须小于图像已提供的层级数。
		 * @param[in] layer 要展示的数组层索引；当前实现要求为 0。
		 * @param[in] channelMask RGBA 低四位通道掩码，至少须启用一个通道。
		 * @param[out] error 输入、纹理获取、界面注册或表面预算失败时输出原因。
		 *
		 * @return 已取得纹理并登记界面令牌，或有效配置与当前预览完全相同时返回 true。
		 */
		[[nodiscard]] bool SetPreviewImage(uint64_t surfaceId, const AssetVersionRef<ImageAsset>&, uint32_t mip,
		    uint32_t layer, uint32_t channelMask, std::string& error);

		/**
		 * @brief 核对请求是否仍对应已发布的预览图像，然后登记该像素的异步拾取。
		 *
		 * @param[in] request 包含表面和场景标识、显示帧序号、目标代次及像素坐标的请求。
		 * @param[out] ticket 成功时接收用于查询或取消该像素读回的票号。
		 * @param[out] error 预览已过期、像素无效或读回登记失败时接收原因。
		 *
		 * @return 成功生成读回票号时返回 true；表面、帧、目标代次或像素边界失配时返回 false。
		 */
		[[nodiscard]] bool RequestPick(const PickingRequest&, uint64_t& ticket, std::string& error);
		[[nodiscard]] bool PollPick(uint64_t ticket, PickingResult& output) { return m_Readbacks.Poll(ticket, output); }
		void CancelPick(uint64_t ticket) noexcept { m_Readbacks.Cancel(ticket); }
		[[nodiscard]] DebugDraw& GetDebugDraw() noexcept { return m_Debug; }
		void SetDebugSurface(uint64_t surfaceId) noexcept { m_DebugSurface = surfaceId; }
		[[nodiscard]] const RendererStatistics& GetStatistics() const noexcept { return m_Statistics; }

	private:
		struct DrawResources;
		struct InstanceGpu;
		struct Targets;
		struct Surface;
		struct QuerySlot;

		[[nodiscard]] RHIStatus BuildFrameGraph(RenderGraph&, const RHIAcquireResult&, const ImGuiFrameData*);
		[[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult&, RHIStatus);
		[[nodiscard]] RHIStatus EnsureSurfaceResources(
		    Surface&, const RenderFramePacket&, Extent2D, uint64_t generation, bool preview);
		[[nodiscard]] RHIStatus PrepareSurface(Surface&, const RenderFramePacket&, Extent2D);
		[[nodiscard]] RHIStatus AddSurfacePasses(RenderGraph&, Surface&, const RenderFramePacket&, bool picking);
		[[nodiscard]] RHIStatus EnsurePipelines(
		    const GeometryGpuResource&, DrawResources&, bool reversedZ, bool mirrored);
		[[nodiscard]] RHIStatus AcquireBuiltinPipeline(std::string_view file, PixelFormat color, PixelFormat depth,
		    RHIDepthState depthState, VertexLayout layout, std::shared_ptr<PipelineResource>& output,
		    RHIBlendMode blend = RHIBlendMode::Opaque, RHITopology topology = RHITopology::TriangleList);
		[[nodiscard]] RHIStatus FillDrawBindings(RenderGraph&, const Surface&, const RenderFramePacket&,
		    const DrawResources&, size_t instanceIndex, const PipelineResource&, bool shadow, RGIndexedDrawDesc&);
		[[nodiscard]] RHIStatus EnsureDefaultTextures();
		[[nodiscard]] RHIStatus AcquireMaterialSampler(uint32_t flags, PixelFormat, SamplerHandle& output);
		[[nodiscard]] Math::MathStatus ResolveEnvironmentSnapshot(
		    RenderFramePacket&, const RenderFramePacket& previous);
		[[nodiscard]] bool IsolatePreviewFailure(uint64_t surfaceId, Surface&, RHIStatus);
		void ClearMainSurface() noexcept;
		void OnSubmitted(RHICompletionPoint, std::span<const RHICompletionPoint>) noexcept;
		void OnCancelled() noexcept;
		[[nodiscard]] RHIStatus RefreshAssetChanges();

		RHIDevice& m_Device;
		RHISwapchain& m_Swapchain;
		AssetManager& m_Assets;
		JobSystem* m_Jobs = nullptr; /* 借用 Runtime 唯一任务调度器，Renderer 先于 Jobs 销毁。 */
		uint64_t m_AssetChangeCursor = 0;
		UploadScheduler m_Uploads;
		ResourceResidencyManager m_Residency;
		/* GPU 资源只由缓存或渲染目标持有，绘制、UI 注册表和读回通过引用延长其生命周期。 */
		GeometryGpuCache m_Geometry;
		TextureGpuCache m_Textures;
		MaterialGpuCache m_Materials;
		PipelineLibrary m_Pipelines;
		ComputePipelineLibrary m_Compute;
		ImGuiRenderer m_UI;
		GameUIPass m_GameUI;
		std::shared_ptr<const UIDrawData> m_GameUIDraw;
		std::shared_ptr<PipelineResource> m_GameUIPipeline;
		ReadbackManager m_Readbacks;
		RenderScene m_Scene;
		RendererProfile m_Profile;
		MainScenePresentation m_MainScenePresentation = MainScenePresentation::MainWindow;
		std::unique_ptr<Surface> m_Main;
		std::map<uint64_t, std::unique_ptr<Surface>> m_Previews;
		std::vector<QuerySlot> m_Queries;
		std::shared_ptr<TextureGpuResource> m_White, m_FlatNormal, m_BlackCube;
		SamplerHandle m_EnvironmentSampler{}, m_ShadowSampler{}, m_NearestEnvironmentSampler{};
		std::map<uint32_t, SamplerHandle> m_MaterialSamplers;
		std::set<std::pair<AssetId, uint64_t>> m_ReportedMaterialFailures;
		std::set<RenderInstanceId> m_ReportedShadowRequests;
		DebugDraw m_Debug;
		uint64_t m_DebugSurface = 0; /* 0仅宿主主场景；其他值只匹配一个离屏surface。 */
		RendererStatistics m_Statistics;
		struct PlatformWindow {
			std::unique_ptr<RHISwapchain> swapchain;
			PlatformWindowPresentation presentation;
		};
		std::map<uint32_t, PlatformWindow> m_PlatformChains;
		PlatformWindowPresentation m_MainPresentation;
		bool m_PlatformFrameReady = false;
		uint32_t m_FrameSlot = 0;
		bool m_QuerySupportChecked = false;
	};
}
