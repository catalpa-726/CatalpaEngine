#pragma once

#include "RHIGpuDependency.h"

// 描述当前一帧使用的命令列表及提交依赖
namespace Catalpa {

    class RHICommandList;

    // commands 借用自设备，在该帧槽下次成功 PrepareFrame 前有效。
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