#pragma once

#include "RHIGpuDependency.h"

// 描述当前一帧使用的命令列表及提交依赖
namespace Catalpa {

    class RHICommandList;

    // commands 借用自设备，仅供当前帧槽代次使用；帧槽复用、失设备清理或设备关闭后须重新获取。
    struct RHIFrameResources {
        RHIFrameToken token{};
        RHICommandList* commands = nullptr;
    };

    // 当前支持一份命令列表，以及一组 AcquireReady / RenderFinished 依赖。
    struct RHISubmission {
        RHIFrameToken frame{};
        RHICommandList* commands = nullptr;
        RHIGpuDependency wait{};
        RHIGpuDependency signal{};
    };

}