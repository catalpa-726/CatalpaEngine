#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHISubmission.h"
#include "RHICompletionPoint.h"

// 公开提交与完成查询，上层不接触 Vulkan 队列
namespace Catalpa {

    // 当前由主线程串行调用。
    class CATALPA_API RHIQueue {
    public:
        virtual ~RHIQueue() = default;

        virtual QueueId GetId() const = 0;

        // 只有提交被接受，completion 才有效。
        [[nodiscard]] virtual RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& completion) = 0;

        // 非阻塞查询该队列上的整批提交是否完成。
        [[nodiscard]] virtual RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) = 0;
    };

}