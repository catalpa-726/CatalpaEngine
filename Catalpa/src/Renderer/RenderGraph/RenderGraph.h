#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"

namespace Catalpa {
    // 保留 Renderer/上传模块的简明入口；声明、编译、录制分别由专职组件实施。
    class RenderGraph : public RenderGraphBuilder {
    public:
        CATALPA_API explicit RenderGraph(RHIDevice& device);

        // 成功才标记已编译；失败输出无效计划且可重试原声明，始终不发布设备状态。
        [[nodiscard]] CATALPA_API RHIStatus Compile(RenderGraphPlan& plan);
    };
}
