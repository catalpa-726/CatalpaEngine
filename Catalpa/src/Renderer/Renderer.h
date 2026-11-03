#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"
#include "Uploads/UploadScheduler.h"

namespace Catalpa {

    class RHIDevice;
    class RHISwapchain;
    class RenderGraph;

    class Renderer {
    public:
        Renderer(RHIDevice& device, RHISwapchain& swapchain) : m_Device(device), m_Swapchain(swapchain), m_Uploads(device) {}
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // 执行一帧：准备、获取、录制、提交、呈现。
        [[nodiscard]] RHIStatus Render();

    private:
        // 声明本帧上传、索引绘制和最终呈现输出。
        [[nodiscard]] RHIStatus BuildFrameGraph(RenderGraph& graph, const RHIAcquireResult& acquired);

        // 录制或提交失败时，收尾尚未消费的 Acquire。
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult& acquired, RHIStatus failure);

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

        // 帧槽进度与管线对应的交换链格式。
        uint32_t m_FrameSlot = 0;
        PixelFormat m_TriangleFormat = PixelFormat::Unknown;
    };

}