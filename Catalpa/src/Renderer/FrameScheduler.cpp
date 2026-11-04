#include "catpch.h"
#include "FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraphPlan.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIQueue.h"

namespace Catalpa {
    RHIStatus FrameScheduler::Execute(const RenderGraphPlan& plan, RHIQueue& queue,
        const RHISubmission& submission, FrameSubmissionResult& result)
    {
        constexpr const char* operation = "FrameScheduler::Execute";
        result.accepted = false;
        result.completion = {};
        result.completions.Clear();
        if (!submission.frame.HasValue() || !submission.commands || !queue.GetId().HasValue()
            || submission.wait.source != submission.frame || submission.signal.source != submission.frame
            || submission.wait.kind != RHIGpuDependencyKind::AcquireReady
            || submission.signal.kind != RHIGpuDependencyKind::RenderFinished)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        try {
            // 当前一个计划对应一份整批完成点；未来多队列扩展不能用最后一批替代全部last-use。
            if (const auto status = result.completions.Reserve(1); !status.IsSuccess()) return status;
            if (const auto status = plan.Record(*submission.commands); !status.IsSuccess()) return status;
            // 编译计划的录制成功必须真的结束命令列表，不能把半录制帧交给队列。
            if (submission.commands->GetState() != RHICommandListState::Executable)
                return { .code = RHIErrorCode::InvalidState, .operation = "FrameScheduler::IncompleteRecording" };
            RHICompletionPoint completion;
            if (const auto status = queue.Submit(submission, completion); !status.IsSuccess()) return status;

            result.accepted = true;
            result.completion = completion;
            // 即使后端违约返回错误完成身份，也绝不能把已经接受的提交当成未提交再取消Acquire。
            if (!completion.HasValue() || completion.device != submission.frame.device || completion.queue != queue.GetId()
                || !result.completions.TryInclude(completion))
                return { .code = RHIErrorCode::InvalidState, .operation = "FrameScheduler::InvalidAcceptedCompletion" };
            return {};
        } catch (const std::bad_alloc&) {
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }
}
