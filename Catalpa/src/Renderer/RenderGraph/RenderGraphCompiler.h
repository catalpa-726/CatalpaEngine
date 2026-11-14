#pragma once

#include "Core/Base.h"
#include "RenderGraphPlan.h"

namespace Catalpa {
	class RenderGraphBuilder;

	/* 编译访问/初始化、DAG及别名生命周期，不录制、不提交、不修改导入的权威状态。 */
	class RenderGraphCompiler {
	public:
		/**
		 * @brief 实际编译路径使用的纯 CPU 分析：范围版本、数据/顺序依赖、稳定拓扑及死 pass 剔除。
		 * exports 均为读取最终版本；初始化检查覆盖全部声明，失败清空 schedule。
		 *
		 * @param[in] resources 图内缓冲区与纹理声明。
		 * @param[in] passes 各任务与访问范围的声明。
		 * @param[in] dependencies 调用者额外指定的任务先后关系。
		 * @param[in] exports 需保留的输出资源。
		 * @param[out] schedule 接收排序后的调度结果。
		 *
		 * @return 返回依赖、初始化范围与拓扑分析结果；编译失败时清空 schedule。
		 */
		[[nodiscard]] CATALPA_API static RHIStatus BuildSchedule(std::span<const RGResourceDeclaration> resources,
		    std::span<const RGPassDeclaration> passes, std::span<const RGDependency> dependencies,
		    std::span<const RGResourceAccess> exports, RGCompiledSchedule& schedule);

		/**
		 * @brief 分析任务依赖与瞬态资源可复用性，生成含状态屏障的录制计划。
		 * 复用同一份 pass 参数产生访问与录制步骤；失败不标记编译成功，不发布设备状态。
		 *
		 * @param[in,out] builder 本次构图保存的资源及任务声明。
		 * @param[out] plan 接收已编译的执行计划。
		 *
		 * @return 返回编译计划的构建状态；失败时不向设备发布资源状态。
		 */
		[[nodiscard]] CATALPA_API static RHIStatus Compile(RenderGraphBuilder& builder, RenderGraphPlan& plan);
	};
}
