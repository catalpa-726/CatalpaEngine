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

        // 执行一帧：准备、获取、录制、提交、呈现。
        [[nodiscard]] RHIStatus Render();

    private:
        // 当前固定清屏内容，后续由场景 pass 替换。
        [[nodiscard]] RHIStatus RecordClear(RHICommandList& commands, const RHIAcquireResult& acquired);

        // 录制或提交失败时，收尾尚未消费的 Acquire。
        [[nodiscard]] RHIStatus CancelFrame(RHIAcquireResult& acquired, RHIStatus failure);

        RHIDevice& m_Device;
        RHISwapchain& m_Swapchain;
        uint32_t m_FrameSlot = 0;
    };

}