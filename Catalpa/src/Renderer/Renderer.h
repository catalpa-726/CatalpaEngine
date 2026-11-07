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

    // 宿主显式选择主窗口是否展示场景；离屏surface的渲染不受此选项影响。
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

    // 拥有表现资源与缓存；所有世界/编辑行为仍通过自有值快照进入。
    class Renderer {
    public:
        Renderer(RHIDevice&, RHISwapchain&, AssetManager&, const RendererProfile& profile = {});
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        [[nodiscard]] RHIStatus Render(const ImGuiFrameData* uiFrame = nullptr);
        void SetJobs(JobSystem* jobs) noexcept {
            m_Jobs = jobs;
        }
        [[nodiscard]] Math::MathStatus SetSceneFrame(RenderFramePacket packet);
        [[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept { return m_Scene.GetFrame(); }
        void ClearSceneFrame() noexcept;
        void SetGameUI(std::shared_ptr<const UIDrawData> data) {
            m_GameUIDraw = std::move(data);
        }
        [[nodiscard]] bool SetPreviewGameUI(uint64_t id, std::shared_ptr<const UIDrawData>,
                                            std::string& error);
        // 默认保留主窗口场景；编辑宿主可只在中央离屏surface展示，交换链仍清底色并合成UI。
        void SetMainScenePresentation(MainScenePresentation) noexcept;
        [[nodiscard]] MainScenePresentation GetMainScenePresentation() const noexcept { return m_MainScenePresentation; }
        [[nodiscard]] RHIStatus InitUI(std::span<const std::byte> pixels, Extent2D extent);

        // 每个surface独占表现目标与帧槽，CPU/GPU资产缓存仍由同一个Renderer共享。
        [[nodiscard]] bool SetPreviewFrame(uint64_t surfaceId, RenderFramePacket packet, std::string& error);
        [[nodiscard]] bool GetPreviewSurface(uint64_t surfaceId, RenderSurfaceSnapshot& output) const;
        void ReleasePreviewSurface(uint64_t surfaceId);
        [[nodiscard]] bool SetPreviewImage(uint64_t surfaceId, const AssetVersionRef<ImageAsset>&,
            uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error);

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
        [[nodiscard]] RHIStatus EnsureSurfaceResources(Surface&, const RenderFramePacket&, Extent2D, uint64_t generation, bool preview);
        [[nodiscard]] RHIStatus PrepareSurface(Surface&, const RenderFramePacket&, Extent2D);
        [[nodiscard]] RHIStatus AddSurfacePasses(RenderGraph&, Surface&, const RenderFramePacket&, bool picking);
        [[nodiscard]] RHIStatus EnsurePipelines(const GeometryGpuResource&, DrawResources&, bool reversedZ, bool mirrored);
        [[nodiscard]] RHIStatus AcquireBuiltinPipeline(std::string_view file, PixelFormat color, PixelFormat depth,
            RHIDepthState depthState, VertexLayout layout, std::shared_ptr<PipelineResource>& output,
            RHIBlendMode blend = RHIBlendMode::Opaque, RHITopology topology = RHITopology::TriangleList);
        [[nodiscard]] RHIStatus FillDrawBindings(RenderGraph&, const Surface&, const RenderFramePacket&,
            const DrawResources&, size_t instanceIndex, const PipelineResource&, bool shadow, RGIndexedDrawDesc&);
        [[nodiscard]] RHIStatus EnsureDefaultTextures();
        [[nodiscard]] RHIStatus AcquireMaterialSampler(uint32_t flags, PixelFormat, SamplerHandle& output);
        [[nodiscard]] Math::MathStatus ResolveEnvironmentSnapshot(RenderFramePacket&, const RenderFramePacket& previous);
        [[nodiscard]] bool IsolatePreviewFailure(uint64_t surfaceId, Surface&, RHIStatus);
        void ClearMainSurface() noexcept;
        void OnSubmitted(RHICompletionPoint, std::span<const RHICompletionPoint>) noexcept;
        void OnCancelled() noexcept;
        [[nodiscard]] RHIStatus RefreshAssetChanges();

        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;
        AssetManager& m_Assets;
        JobSystem* m_Jobs = nullptr; // 借用 Runtime 唯一任务调度器，Renderer 先于 Jobs 销毁。
        uint64_t m_AssetChangeCursor = 0;
        UploadScheduler m_Uploads;
        ResourceResidencyManager m_Residency;
        // 逻辑GPU owner只存在于缓存/Targets中；draw、UI注册表和读回仅保活lease。
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
        uint64_t m_DebugSurface = 0; // 0仅宿主主场景；其他值只匹配一个离屏surface。
        RendererStatistics m_Statistics;
        uint32_t m_FrameSlot = 0;
        bool m_QuerySupportChecked = false;
    };
}
