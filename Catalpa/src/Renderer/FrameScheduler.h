#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "RHI/RHICompletionSet.h"
#include "RHI/RHISubmission.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	class RenderGraphPlan;
	class RHIQueue;
	class JobSystem;

	/* 一次执行的真实提交事实。accepted不代表GPU完成，也不因后续呈现失败而撤销。 */
	struct FrameSubmissionResult {
		bool accepted = false;
		RHICompletionPoint completion{};
		RHICompletionSet completions;
		bool anyAccepted = false;
		uint32_t acceptedBatchCount = 0;
		uint32_t parallelRecordedBatchCount = 0;
		/* 图中实际队列切换过多时退回同一graphics队列，保持功能及所有权证明。 */
		bool serializedForBudget = false;
	};

	/*
	 * 稳定拓扑批次、真实多queue完成点和跨family所有权转移共用唯一接受账本。
	 * 帧槽准备/Acquire/Present仍由宿主渲染循环与RHI负责，不建立第二套等待或回收器。
	 */
	class FrameScheduler {
	public:
		/**
		 * @brief 校验资源版本，将渲染图分批录制并提交，同时记录实际已受理的队列完成点。
		 * 提交前为完成点预留容量。中途失败时，已接收批次仍须等各自完成后才能复用资源。
		 *
		 * @param[in] plan 已编译的资源和命令调度计划。
		 * @param[in,out] queue 主提交队列。
		 * @param[in] submission 携带帧令牌以及获取和呈现同步依赖的提交描述。
		 * @param[out] result 输出各队列已接受的完成点；anyAccepted 表示至少一个批次被接受，accepted 仅表示呈现批次被接受。
		 * @param[in] jobs 可选的并行命令录制作业系统，为空时在当前线程录制。
		 * @param[in] asyncQueues 是否启用独立队列调度；关闭时由主图形队列串行提交。
		 *
		 * @return 返回调度及提交结果；即使返回错误，也须根据 result 回收之前已受理的批次。
		 */
		[[nodiscard]] static CATALPA_API RHIStatus Execute(const RenderGraphPlan& plan, RHIQueue& queue,
		    const RHISubmission& submission, FrameSubmissionResult& result, JobSystem* jobs = nullptr,
		    bool asyncQueues = true);
	};
} /* 命名空间 Catalpa */
