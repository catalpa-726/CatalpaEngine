#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "Uploads/UploadScheduler.h"
#include "UI/ImGuiRenderer.h"
#include "Renderer/RenderScene.h"
#include "Renderer/RendererFrameContext.h"

namespace Catalpa {

    class RHIDevice;
    class RHISwapchain;
    class RenderGraph;

    class Renderer {
    public:
        Renderer(RHIDevice& device, RHISwapchain& swapchain, const RendererProfile& profile = {})
            : m_Device(device), m_Swapchain(swapchain), m_Uploads(device), m_UI(device), m_FrameUniforms(device), m_Profile(profile) {}
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // 执行一帧：准备、获取、录制、提交、呈现。
        [[nodiscard]] RHIStatus Render(const ImGuiFrameData* uiFrame = nullptr);
        // 接收并拥有CPU快照；GPU资源仅由渲染方经现有设备/上传链派生。
        [[nodiscard]] Math::MathStatus SetSceneFrame(RenderFramePacket packet) { return m_Scene.Apply(std::move(packet)); }
        [[nodiscard]] const RenderFramePacket& GetSceneFrame() const noexcept { return m_Scene.GetFrame(); }
        void ClearSceneFrame() noexcept { m_Scene.Clear(); m_FrameUniforms.ClearHistory(); }

        // 将固定字体 atlas 交给同一上传链；UI 帧通过自有 CPU packet 传入。
        [[nodiscard]] RHIStatus InitUI(std::span<const std::byte> pixels, Extent2D extent) { return m_UI.InitFont(m_Uploads, pixels, extent); }

    private:
        // 声明本帧上传、索引绘制和最终呈现输出。
        [[nodiscard]] RHIStatus BuildFrameGraph(RenderGraph& graph, const RHIAcquireResult& acquired, const ImGuiFrameData* uiFrame);

        // 录制或提交失败时，收尾尚未消费的 Acquire。
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult& acquired, RHIStatus failure);

        // 密封CPU对象是本步稳定cache key；正式资产版本键由S04接管，不另建资产管理器。
        struct GeometryGpu {
            MeshResourceRef mesh;
            BufferHandle vertices{}, indices{};
            GraphicsPipelineHandle forward{}, depth{};
            PixelFormat colorFormat = PixelFormat::Unknown;
            bool reversedZ = false;
            bool prepass = false;
            bool uploadPending = true;
        };
        [[nodiscard]] RHIStatus EnsureSceneResources();
        [[nodiscard]] RHIStatus EnsureScenePipelines(GeometryGpu& geometry, bool reversedZ);
        [[nodiscard]] RHIStatus EnsureDepthTarget(Extent2D extent);
        // pending上传仍依赖目标owner，不在取消帧时丢弃；已提交资源可由命令引用继续保活。
        void PruneGeometry() noexcept;
        void ReleaseGeometry(GeometryGpu& geometry) noexcept;

        // 按交换链实际格式创建或替换管线。
        [[nodiscard]] RHIStatus EnsureTrianglePipeline();

        // 分别幂等创建静态几何，上传由当前帧统一录制。
        [[nodiscard]] RHIStatus EnsureTriangleGeometry();

        // 幂等创建并安排棋盘纹理上传，供两次绘制共享。
        [[nodiscard]] RHIStatus EnsureTriangleTexture();

        // 幂等创建棋盘纹理使用的 Sampler。
        [[nodiscard]] RHIStatus EnsureTriangleSampler();

    private:
        // 持有管线、静态几何、纹理和 Sampler 的逻辑所有权；设备负责原生资源生命周期。
        GraphicsPipelineHandle m_TrianglePipeline{};

        BufferHandle m_TriangleVertices{};
        BufferHandle m_TriangleIndices{};

        TextureHandle m_TriangleTexture{};
        TextureViewHandle m_TriangleTextureView{};
        SamplerHandle m_TriangleSampler{};

        // 借用 Runtime 持有的设备和交换链。
        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;
        UploadScheduler m_Uploads;
        ImGuiRenderer m_UI;
        RenderScene m_Scene;
        RendererFrameContext m_FrameUniforms;
        RendererProfile m_Profile;
        std::map<const MeshResource*, GeometryGpu> m_Geometry;
        TextureHandle m_DepthTexture{};
        TextureViewHandle m_DepthView{};
        Extent2D m_DepthExtent{};

        // 帧槽进度与管线对应的交换链格式。
        uint32_t m_FrameSlot = 0;
        PixelFormat m_TriangleFormat = PixelFormat::Unknown;
    };

}
