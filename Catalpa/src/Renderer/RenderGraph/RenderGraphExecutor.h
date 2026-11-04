#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraphPlan;
    class RHICommandList;

    // 按不可变计划录制屏障及 pass，并将局部状态增量交给 command list；提交归 FrameScheduler。
    class RenderGraphExecutor {
    public:
        [[nodiscard]] CATALPA_API static RHIStatus Record(const RenderGraphPlan& plan, RHICommandList& commands);
    };
}
