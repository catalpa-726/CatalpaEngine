#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraphPlan;
	class RHICommandList;

	/* 按不可变计划录制屏障及渲染通道，将本地状态变更移交命令列表，由 FrameScheduler 负责提交。 */
	class RenderGraphExecutor {
	public:
		/**
		 * @brief 按已编译计划录制命令，资源最终状态留待队列受理提交后发布。
		 *
		 * @param[in] plan 已编译的通道顺序、屏障、资源状态更新和可选的时间查询。
		 * @param[in,out] commands 可开始录制的命令列表，依次接收屏障、通道操作和查询。
		 *
		 * @return 成功表示命令列表已结束录制；失败时返回首个错误，已经录制的前缀可能保留。
		 */
		[[nodiscard]] CATALPA_API static RHIStatus Record(const RenderGraphPlan& plan, RHICommandList& commands);
	};
}
