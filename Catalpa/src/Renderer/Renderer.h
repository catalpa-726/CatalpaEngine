#pragma once
#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "Uploads/UploadScheduler.h"
#include "UI/ImGuiRenderer.h"
#include "Renderer/RenderScene.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/MaterialGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
namespace Catalpa {
    class RHIDevice; class RHISwapchain; class RenderGraph; class AssetManager;
    class Renderer {
    public:
        Renderer(RHIDevice&, RHISwapchain&, AssetManager&, const RendererProfile& profile = {});
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        [[nodiscard]] RHIStatus Render(const ImGuiFrameData* uiFrame = nullptr);
        [[nodiscard]] Math::MathStatus SetSceneFrame(RenderFramePacket packet) { return m_Scene.Apply(std::move(packet)); }
        [[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept { return m_Scene.GetFrame(); }
        void ClearSceneFrame() noexcept { m_Scene.Clear(); m_FrameUniforms.ClearHistory(); m_Instances.clear(); }
        // 字体CPU载荷同样发布为固定版本并进入统一TextureGpuCache。
        [[nodiscard]] RHIStatus InitUI(std::span<const std::byte> pixels, Extent2D extent);
    private:
        struct InstanceGpu {
            std::shared_ptr<GeometryGpuResource> geometry;
            std::vector<std::shared_ptr<MaterialGpuResource>> materials;
            std::vector<std::shared_ptr<PipelineResource>> forward, depth;
        };
        [[nodiscard]] RHIStatus BuildFrameGraph(RenderGraph&, const RHIAcquireResult&, const ImGuiFrameData*);
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult&, RHIStatus);
        [[nodiscard]] RHIStatus EnsureSceneResources();
        [[nodiscard]] RHIStatus EnsureScenePipelines(const MeshData&, const MaterialGpuResource&, bool reversedZ,
            std::shared_ptr<PipelineResource>& forward, std::shared_ptr<PipelineResource>& depth);
        [[nodiscard]] RHIStatus EnsureDepthTarget(Extent2D);
        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;
        AssetManager& m_Assets;
        UploadScheduler m_Uploads;
        // 缓存是唯一GPU逻辑owner；UI/instance只借用保活lease，设备仍独占原生资源退休。
        GeometryGpuCache m_Geometry;
        TextureGpuCache m_Textures;
        MaterialGpuCache m_Materials;
        PipelineLibrary m_Pipelines;
        ImGuiRenderer m_UI;
        RenderScene m_Scene;
        RendererFrameContext m_FrameUniforms;
        RendererProfile m_Profile;
        std::vector<InstanceGpu> m_Instances;
        std::set<std::pair<AssetId,uint64_t>> m_ReportedMaterialFailures;
        TextureHandle m_DepthTexture{};
        TextureViewHandle m_DepthView{};
        Extent2D m_DepthExtent{};
        uint32_t m_FrameSlot = 0;
    };
}
