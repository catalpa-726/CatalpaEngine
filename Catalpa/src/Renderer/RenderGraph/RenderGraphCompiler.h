#pragma once

#include "Core/Base.h"
#include "RenderGraphPlan.h"

namespace Catalpa {
    class RenderGraphBuilder;

    // 编译访问/初始化、DAG及别名生命周期，不录制、不提交、不修改导入的权威状态。
    class RenderGraphCompiler {
    public:
        // 实际编译路径使用的纯 CPU 分析：范围版本、数据/顺序依赖、稳定拓扑及死 pass 剔除。
        // exports 均为读取最终版本；初始化检查覆盖全部声明，失败清空 schedule。
        [[nodiscard]] CATALPA_API static RHIStatus BuildSchedule(
            std::span<const RGResourceDeclaration> resources, std::span<const RGPassDeclaration> passes,
            std::span<const RGDependency> dependencies, std::span<const RGResourceAccess> exports,
            RGCompiledSchedule& schedule);

        // 复用同一份 pass 参数产生访问与录制步骤；失败不标记编译成功，不发布设备状态。
        [[nodiscard]] CATALPA_API static RHIStatus Compile(RenderGraphBuilder& builder, RenderGraphPlan& plan);
    };
}
