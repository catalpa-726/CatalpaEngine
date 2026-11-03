#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHISwapchainDesc.h"

namespace Catalpa {

    class RHIDevice;
    class RHISwapchain;
    class RHICommandList;

    class Renderer {
    public:
        Renderer(RHIDevice& device, RHISwapchain& swapchain) : m_Device(device), m_Swapchain(swapchain) {}
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // 执行一帧：准备、获取、录制、提交、呈现。
        [[nodiscard]] RHIStatus Render();

    private:
        // 录制交换链图像布局转换、清屏和当前三角形绘制。
        [[nodiscard]] RHIStatus RecordFrame(RHICommandList& commands, const RHIAcquireResult& acquired);

        // 录制或提交失败时，收尾尚未消费的 Acquire。
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult& acquired, RHIStatus failure);

        // 按交换链实际格式创建或替换管线。
        [[nodiscard]] RHIStatus EnsureTrianglePipeline();

    private:
        // 持有管线的逻辑所有权；设备负责原生资源生命周期。
        GraphicsPipelineHandle m_TrianglePipeline{};

        // 借用 Runtime 持有的设备和交换链。
        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;

        // 帧槽进度与管线对应的交换链格式。
        uint32_t m_FrameSlot = 0;
        PixelFormat m_TriangleFormat = PixelFormat::Unknown;
    };

}