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

    // 一次执行的真实提交事实。accepted不代表GPU完成，也不因后续呈现失败而撤销。
    struct FrameSubmissionResult {
        bool accepted = false;
        RHICompletionPoint completion{};
        RHICompletionSet completions;
        bool anyAccepted = false;
        uint32_t acceptedBatchCount = 0;
        uint32_t parallelRecordedBatchCount = 0;
        // 图中实际队列切换过多时退回同一graphics队列，保持功能及所有权证明。
        bool serializedForBudget = false;
    };

    // 稳定拓扑批次、真实多queue完成点和跨family所有权转移共用唯一接受账本。
    // 帧槽准备/Acquire/Present仍由宿主渲染循环与RHI负责，不建立第二套等待或回收器。
    class FrameScheduler {
      public:
        // accepted仅表示WSI批次接受；anyAccepted/completions同时保存失败前的真实前缀。
        // prefix失败可收尾Acquire，auxiliary命令和帧资源仍须等各自完成点后再复用。
        // 整批完成记录在提交前预留容量，队列接受后不再分配内存。
        [[nodiscard]] static CATALPA_API RHIStatus Execute(const RenderGraphPlan& plan, RHIQueue& queue,
                                                           const RHISubmission& submission,
                                                           FrameSubmissionResult& result,
                                                           JobSystem* jobs = nullptr,
                                                           bool asyncQueues = true);
    };
} // namespace Catalpa
