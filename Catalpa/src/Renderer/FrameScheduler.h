#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "RHI/RHICompletionSet.h"
#include "RHI/RHISubmission.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraphPlan;
    class RHIQueue;

    // 一次执行的真实提交事实。accepted不代表GPU完成，也不因后续呈现失败而撤销。
    struct FrameSubmissionResult {
        bool accepted = false;
        RHICompletionPoint completion{};
        RHICompletionSet completions;
    };

    // 单graphics队列阶段的执行调度：只按不可变计划录制一次并提交一次。
    // 帧槽准备/Acquire/Present仍由宿主渲染循环与RHI负责，不建立第二套等待或回收器。
    class FrameScheduler {
    public:
        // 失败时检查result.accepted：false允许收尾Acquire；true必须保留已提交事实。
        // 整批完成记录在提交前预留容量，队列接受后不再分配内存。
        [[nodiscard]] static CATALPA_API RHIStatus Execute(const RenderGraphPlan& plan, RHIQueue& queue,
            const RHISubmission& submission, FrameSubmissionResult& result);
    };
}
