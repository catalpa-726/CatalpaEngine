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
        // 录制清屏和当前最小绘制。
        [[nodiscard]] RHIStatus RecordFrame(RHICommandList& commands, const RHIAcquireResult& acquired);

        // 录制或提交失败时，收尾尚未消费的 Acquire。
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult& acquired, RHIStatus failure);

        // 按交换链实际格式创建或替换管线。
        [[nodiscard]] RHIStatus EnsureTrianglePipeline();

    private:
        GraphicsPipelineHandle m_TrianglePipeline{};

        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;
        uint32_t m_FrameSlot = 0;
        PixelFormat m_TriangleFormat = PixelFormat::Unknown;

    };

}