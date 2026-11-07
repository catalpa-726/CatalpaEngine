#include "catpch.h"
#include "FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraphPlan.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIQueue.h"
#include "RHI/RHIDevice.h"
#include "Core/Jobs/JobSystem.h"

namespace Catalpa
{
    RHIStatus FrameScheduler::Execute(const RenderGraphPlan& plan, RHIQueue& queue,
                                      const RHISubmission& submission, FrameSubmissionResult& result,
                                      JobSystem* jobs, bool asyncQueues)
    try {
        constexpr const char* operation = "FrameScheduler::Execute";
        result.accepted = false;
        result.anyAccepted = false;
        result.acceptedBatchCount = 0;
        result.parallelRecordedBatchCount = 0;
        result.serializedForBudget = false;
        result.completion = {};
        result.completions.Clear();
        if (!plan.IsValid() || !submission.frame.HasValue() || !submission.commands ||
            !queue.GetId().HasValue() || submission.wait.source != submission.frame ||
            submission.signal.source != submission.frame ||
            submission.wait.kind != RHIGpuDependencyKind::AcquireReady ||
            submission.signal.kind != RHIGpuDependencyKind::RenderFinished)
            return {RHIErrorCode::InvalidDescription, 0, operation};
        if (const auto status = result.completions.Reserve(4); !status.IsSuccess())
            return status;
        const auto accept = [&](RHICompletionPoint point, bool presentation) -> RHIStatus {
            // GPU接受是不可回滚的事实，先标记，再检查后端返回身份。
            result.anyAccepted = true;
            ++result.acceptedBatchCount;
            if (presentation) {
                result.accepted = true;
                result.completion = point;
            }
            if (!point.HasValue() || point.device != submission.frame.device ||
                !result.completions.TryInclude(point))
                return {RHIErrorCode::InvalidState, 0, "FrameScheduler/AcceptedCompletionIdentity"};
            return {};
        };
        auto* device = queue.GetDevice();
        if (!device || !device->SupportsScheduledBatches()) {
            if (const auto status = plan.Record(*submission.commands); !status.IsSuccess())
                return status;
            RHICompletionPoint point;
            if (const auto status = queue.Submit(submission, point); !status.IsSuccess())
                return status;
            return accept(point, true);
        }
        // 编译与执行之间的外部状态变化必须先拒绝；后面的revision只由已接受批次推进。
        for (const auto& update : plan.m_BufferUpdates) {
            RHIBufferStateSnapshot state;
            if (const auto s = device->GetBufferState(update.buffer, state); !s.IsSuccess())
                return s;
            if (state.state.revision != update.expectedRevision)
                return {RHIErrorCode::InvalidState, 0, "FrameScheduler/StaleBufferPlan"};
        }
        for (const auto& update : plan.m_TextureUpdates) {
            RHITextureStateSnapshot state;
            if (const auto s = device->GetTextureState(update.view, state); !s.IsSuccess())
                return s;
            if (state.state.revision != update.expectedRevision)
                return {RHIErrorCode::InvalidState, 0, "FrameScheduler/StaleTexturePlan"};
        }
        struct Batch
        {
            RenderGraphPlan plan;
            RHIQueue* queue = nullptr;
            RHIFrameResources commands;
            RHICompletionSet dependencies;
            RHIStatus recorded;
            bool presentation = false;
            std::vector<uint32_t> passes;
            size_t index = 0;
        };
        // draw数量不等于提交数量。只按实际queue切换分批，依赖工作区按节点/边线性有界。
        constexpr size_t maxBatches = 128;
        constexpr uint32_t maxPasses = 262144;
        constexpr size_t maxEdges = 4194304;
        constexpr uint32_t maxContexts = 1280;
        uint32_t ordinal = 0;
        std::vector<std::unique_ptr<Batch>> batches;
        batches.reserve(std::min(plan.m_Steps.size() + 1, maxBatches + 1));
        // WSI图像只进入最后的graphics批次，Acquire/RenderFinished恰好各使用一次。
        std::vector<const RenderGraphPlan::Step*> ordered;
        std::set<uint32_t> wsiPasses;
        const auto isWSI = [&](const auto& step) {
            return std::any_of(step.textureStates.begin(), step.textureStates.end(),
                               [&](const auto& u) { return u.view == plan.m_PresentView; });
        };
        for (const auto& step : plan.m_Steps)
            if (isWSI(step) && step.sourcePass != UINT32_MAX)
                wsiPasses.insert(step.sourcePass);
        // 只能把无向外依赖的WSI子图下沉；否则保持原拓扑，以单一graphics批次处理整图。
        bool sinkWSI = true;
        for (const auto& edge : plan.m_Schedule.dependencies)
            if (wsiPasses.contains(edge.before) && !wsiPasses.contains(edge.after)) {
                sinkWSI = false;
                break;
            }
        if (sinkWSI) {
            for (const auto& step : plan.m_Steps)
                if (!isWSI(step))
                    ordered.push_back(&step);
            for (const auto& step : plan.m_Steps)
                if (isWSI(step))
                    ordered.push_back(&step);
        }
        else
            for (const auto& step : plan.m_Steps)
                ordered.push_back(&step);
        size_t runCount = 0;
        RHIQueue* previousQueue = nullptr;
        bool previousPresentation = false;
        for (const auto* step : ordered) {
            const bool presentation = !sinkWSI || isWSI(*step);
            auto* target = presentation || !asyncQueues ? &queue : &device->GetQueue(step->queue);
            if (target != previousQueue || presentation != previousPresentation)
                ++runCount;
            previousQueue = target;
            previousPresentation = presentation;
        }
        const bool serialBudgetFallback = runCount > maxBatches;
        if ((!asyncQueues || serialBudgetFallback || !sinkWSI) &&
            !device->GetQueueInfo(queue.GetId()).compute &&
            std::any_of(ordered.begin(), ordered.end(),
                        [](const auto* step) { return step->queue == RHIQueueClass::Compute; }))
            return {RHIErrorCode::Unsupported, 0, "FrameScheduler/SerialGraphicsRequiresCompute"};
        result.serializedForBudget = serialBudgetFallback;
        for (const auto* source : ordered) {
            const auto& step = *source;
            const bool presentationTail = !sinkWSI || isWSI(step);
            RHIQueue* target = presentationTail || !asyncQueues || serialBudgetFallback
                                   ? &queue
                                   : &device->GetQueue(step.queue);
            const bool append = !batches.empty() && batches.back()->presentation == presentationTail &&
                                batches.back()->queue == target;
            if (!append) {
                auto batch = std::make_unique<Batch>();
                batch->queue = target;
                batch->presentation = presentationTail;
                batch->plan.m_Valid = true;
                batch->plan.m_QueryBegin = false;
                batch->plan.m_QueryEnd = false;
                batch->index = batches.size();
                batches.push_back(std::move(batch));
            }
            auto& batch = *batches.back();
            batch.plan.m_Steps.push_back(step);
            if (step.sourcePass != UINT32_MAX)
                batch.passes.push_back(step.sourcePass);
            for (const auto& update : step.bufferStates) {
                const auto found =
                    std::find_if(batch.plan.m_BufferUpdates.begin(), batch.plan.m_BufferUpdates.end(),
                                 [&](const auto& u) { return u.buffer == update.buffer; });
                if (found == batch.plan.m_BufferUpdates.end())
                    batch.plan.m_BufferUpdates.push_back(update);
                else {
                    const bool writes = found->writes || update.writes;
                    *found = update;
                    found->writes = writes;
                }
            }
            for (const auto& update : step.textureStates) {
                const auto found =
                    std::find_if(batch.plan.m_TextureUpdates.begin(), batch.plan.m_TextureUpdates.end(),
                                 [&](const auto& u) { return u.view == update.view; });
                if (found == batch.plan.m_TextureUpdates.end())
                    batch.plan.m_TextureUpdates.push_back(update);
                else {
                    const bool writes = found->writes || update.writes;
                    *found = update;
                    found->writes = writes;
                }
            }
        }
        if (batches.empty() || !batches.back()->presentation)
            return {RHIErrorCode::InvalidState, 0, "FrameScheduler/MissingPresentationTail"};
        uint32_t maxPass = 0;
        for (const auto& batch : batches)
            for (auto pass : batch->passes)
                maxPass = std::max(maxPass, pass);
        for (const auto& edge : plan.m_Schedule.dependencies)
            maxPass = std::max({maxPass, edge.before, edge.after});
        if (maxPass >= maxPasses || plan.m_Schedule.dependencies.size() > maxEdges)
            return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/DependencyProofBudget"};
        const size_t nodeCount = size_t(maxPass) + 1;
        // CSR反向邻接表复用一个访问标记/栈；不会创建每个pass的完整可达矩阵。
        std::vector<uint32_t> reverseOffsets(nodeCount + 1, 0);
        for (const auto& edge : plan.m_Schedule.dependencies)
            ++reverseOffsets[size_t(edge.after) + 1];
        for (size_t i = 1; i < reverseOffsets.size(); ++i)
            reverseOffsets[i] += reverseOffsets[i - 1];
        std::vector<uint32_t> reverseEdges(plan.m_Schedule.dependencies.size());
        auto nextOffset = reverseOffsets;
        for (const auto& edge : plan.m_Schedule.dependencies)
            reverseEdges[nextOffset[edge.after]++] = edge.before;
        std::vector<uint32_t> passBatch(nodeCount, UINT32_MAX);
        for (const auto& batch : batches)
            for (auto pass : batch->passes)
                passBatch[pass] = static_cast<uint32_t>(batch->index);
        // 关系表只按最多128个实际提交批次建表，而非按几万条draw建表。
        std::vector<std::vector<uint8_t>> ancestors(batches.size(), std::vector<uint8_t>(batches.size()));
        std::vector<uint8_t> visited(nodeCount);
        std::vector<uint32_t> stack;
        stack.reserve(nodeCount);
        for (const auto& batch : batches) {
            std::fill(visited.begin(), visited.end(), 0);
            stack.clear();
            for (auto pass : batch->passes)
                if (!visited[pass]) {
                    visited[pass] = 1;
                    stack.push_back(pass);
                }
            while (!stack.empty()) {
                const auto pass = stack.back();
                stack.pop_back();
                const auto priorBatch = passBatch[pass];
                if (priorBatch != UINT32_MAX && priorBatch != batch->index)
                    ancestors[batch->index][priorBatch] = 1;
                for (size_t i = reverseOffsets[pass]; i < reverseOffsets[size_t(pass) + 1]; ++i) {
                    const auto prior = reverseEdges[i];
                    if (!visited[prior]) {
                        visited[prior] = 1;
                        stack.push_back(prior);
                    }
                }
            }
        }
        std::vector<RHICompletionPoint> batchPoints(batches.size());
        RHICompletionPoint queryStart;
        const auto prepareCommands = [&](Batch& batch) -> RHIStatus {
            if (batch.presentation) {
                batch.commands = {submission.frame, submission.commands};
                return {};
            }
            if (ordinal >= maxContexts)
                return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/BatchContextBudget"};
            return device->PrepareAuxiliaryCommands(submission.frame, batch.queue->GetId(), ordinal++,
                                                    batch.commands);
        };
        const auto submit = [&](Batch& batch) -> RHIStatus {
            RHISubmission send{.frame = submission.frame, .commands = batch.commands.commands};
            if (batch.presentation) {
                send.wait = submission.wait;
                send.signal = submission.signal;
            }
            send.dependencies = batch.dependencies.GetPoints();
            RHICompletionPoint point;
            if (const auto status = batch.queue->Submit(send, point); !status.IsSuccess())
                return status;
            if (const auto status = accept(point, batch.presentation); !status.IsSuccess())
                return status;
            if (!batch.passes.empty())
                batchPoints[batch.index] = point;
            return {};
        };
        const auto include = [&](Batch& batch, RHICompletionPoint point) -> RHIStatus {
            if (point.HasValue() && !batch.dependencies.TryInclude(point))
                return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/DependencyCapacity"};
            return {};
        };
        const auto frontier = [&](Batch& batch, const auto& state, bool writes) -> RHIStatus {
            if (const auto status = include(batch, state.frontier.writer); !status.IsSuccess())
                return status;
            if (writes || state.ownership.IsReleased())
                for (auto point : state.frontier.readers.GetPoints())
                    if (const auto status = include(batch, point); !status.IsSuccess())
                        return status;
            if (state.ownership.IsReleased())
                return include(batch, state.ownership.releasePoint);
            return {};
        };
        const auto immediate = [&](Batch& batch) -> RHIStatus {
            if (const auto status = prepareCommands(batch); !status.IsSuccess())
                return status;
            if (const auto status = batch.plan.Record(*batch.commands.commands); !status.IsSuccess())
                return status;
            return submit(batch);
        };
        if (plan.m_Queries) {
            Batch kickoff;
            kickoff.queue = &queue;
            kickoff.plan.m_Valid = true;
            kickoff.plan.m_Queries = plan.m_Queries;
            kickoff.plan.m_QueryEnd = false;
            if (const auto s = kickoff.dependencies.Reserve(4); !s.IsSuccess())
                return s;
            if (const auto s = immediate(kickoff); !s.IsSuccess())
                return s;
            queryStart = result.completions.GetPoints().back();
        }
        // 已接受release可以跨帧保留。若消费者提交失败，下一帧先补配对acquire再转向新family。
        const auto ownership = [&](Batch& consumer) -> RHIStatus {
            // 同轮同queue的所有资源共用一个release/acquire批次；资源数不会放大命令池数。
            // 错向pending release最多先acquire一次、再release到当前消费者，第三轮仅验证。
            for (unsigned repair = 0; repair < 3; ++repair) {
                std::vector<std::unique_ptr<Batch>> transfers;
                const auto append = [&](BufferHandle buffer, TextureViewHandle texture) -> RHIStatus {
                    RHIBufferStateSnapshot b;
                    RHITextureStateSnapshot t;
                    if (buffer.HasValue()) {
                        if (const auto s = device->GetBufferState(buffer, b); !s.IsSuccess())
                            return s;
                    }
                    else if (const auto s = device->GetTextureState(texture, t); !s.IsSuccess())
                        return s;
                    const auto state = buffer.HasValue() ? b.state.ownership : t.state.ownership;
                    const auto target = device->GetQueueInfo(consumer.queue->GetId());
                    if (!state.owner.HasValue() ||
                        (!state.IsReleased() && device->GetQueueInfo(state.owner).family == target.family) ||
                        (state.IsReleased() && state.releasedTo == target.id))
                        return {};
                    auto* transferQueue =
                        device->GetQueue(state.IsReleased() ? state.releasedTo : state.owner);
                    if (!transferQueue)
                        return {RHIErrorCode::InvalidState, 0, "FrameScheduler/OwnershipQueueMissing"};
                    auto group = std::find_if(transfers.begin(), transfers.end(), [&](const auto& value) {
                        return value->queue == transferQueue;
                    });
                    if (group == transfers.end()) {
                        auto value = std::make_unique<Batch>();
                        value->queue = transferQueue;
                        value->plan.m_Valid = true;
                        value->plan.m_QueryBegin = false;
                        value->plan.m_QueryEnd = false;
                        if (const auto s = value->dependencies.Reserve(4); !s.IsSuccess())
                            return s;
                        if (const auto s = include(*value, queryStart); !s.IsSuccess())
                            return s;
                        RenderGraphPlan::Step step;
                        step.name = "Grouped queue-family ownership repair";
                        step.kind = RenderGraphPlan::StepKind::Transition;
                        value->plan.m_Steps.push_back(std::move(step));
                        transfers.push_back(std::move(value));
                        group = transfers.end() - 1;
                    }
                    auto& transfer = **group;
                    auto& step = transfer.plan.m_Steps.front();
                    const RHIQueueTransfer proof{
                        state.owner, state.IsReleased() ? state.releasedTo : target.id,
                        state.IsReleased() ? RHIOwnershipPhase::Acquire : RHIOwnershipPhase::Release};
                    auto after = state;
                    if (state.IsReleased()) {
                        after = {};
                        after.owner = state.releasedTo;
                    }
                    else {
                        after.releasedTo = target.id;
                        after.releasePoint = {};
                        after.releaseOldLayout =
                            buffer.HasValue() ? RHITextureLayout::Undefined : t.state.access.layout;
                        after.releaseNewLayout = after.releaseOldLayout;
                    }
                    if (buffer.HasValue()) {
                        if (const auto s = frontier(transfer, b.state, true); !s.IsSuccess())
                            return s;
                        auto value = b.state;
                        if (value.revision == UINT64_MAX)
                            return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/RevisionExhausted"};
                        ++value.revision;
                        value.ownership = after;
                        transfer.plan.m_BufferUpdates.push_back(
                            {buffer, b.state.revision, std::move(value), false});
                        const RHIBufferAccess access{RHIPipelineStage::AllCommands,
                                                     RHIResourceAccess::MemoryReadWrite};
                        step.bufferBarriers.push_back({buffer, 0, b.desc.size, access, access, proof});
                    }
                    else {
                        if (t.presentable)
                            return {RHIErrorCode::InvalidState, 0, "FrameScheduler/WSIOwnership"};
                        if (const auto s = frontier(transfer, t.state, true); !s.IsSuccess())
                            return s;
                        auto value = t.state;
                        if (value.revision == UINT64_MAX)
                            return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/RevisionExhausted"};
                        ++value.revision;
                        value.ownership = after;
                        value.access.stage = RHIPipelineStage::AllCommands;
                        value.access.access = RHIResourceAccess::None;
                        transfer.plan.m_TextureUpdates.push_back(
                            {texture, t.state.revision, std::move(value), false});
                        auto before = t.state.access, afterAccess = t.state.access;
                        if (state.IsReleased()) {
                            before.layout = state.releaseOldLayout;
                            afterAccess.layout = state.releaseNewLayout;
                        }
                        afterAccess.stage = RHIPipelineStage::AllCommands;
                        afterAccess.access = RHIResourceAccess::None;
                        step.textureBarriers.push_back(
                            {.view = texture,
                             .range = {.levelCount = t.mipLevels,
                                       .layerCount = t.arrayLayers,
                                       .aspect = GetPixelFormatInfo(t.format).isDepth
                                                     ? RHITextureAspect::Depth
                                                     : RHITextureAspect::Color},
                             .before = before,
                             .after = afterAccess,
                             .transfer = proof});
                    }
                    return {};
                };
                for (const auto& update : consumer.plan.m_BufferUpdates)
                    if (const auto s = append(update.buffer, {}); !s.IsSuccess())
                        return s;
                for (const auto& update : consumer.plan.m_TextureUpdates)
                    if (const auto s = append({}, update.view); !s.IsSuccess())
                        return s;
                if (transfers.empty())
                    return {};
                for (auto& transfer : transfers)
                    if (const auto s = immediate(*transfer); !s.IsSuccess())
                        return s;
            }
            return {RHIErrorCode::InvalidState, 0, "FrameScheduler/OwnershipRepairDidNotConverge"};
        };
        const auto bind = [&](Batch& batch) -> RHIStatus {
            if (const auto status = batch.dependencies.Reserve(4); !status.IsSuccess())
                return status;
            for (auto point : submission.dependencies)
                if (const auto s = include(batch, point); !s.IsSuccess())
                    return s;
            if (const auto s = include(batch, queryStart); !s.IsSuccess())
                return s;
            // 剔除的中间pass仍可能传递Explicit/WAR约束，跨queue等待必须保留其可达祖先。
            for (size_t prior = 0; prior < batches.size(); ++prior)
                if (ancestors[batch.index][prior])
                    if (const auto s = include(batch, batchPoints[prior]); !s.IsSuccess())
                        return s;
            if (const auto s = ownership(batch); !s.IsSuccess())
                return s;
            RenderGraphPlan::Step pre;
            pre.name = "Accepted batch ownership acquire";
            pre.kind = RenderGraphPlan::StepKind::Transition;
            RenderGraphPlan::Step visibility;
            visibility.name = "Batch initial texture visibility";
            visibility.kind = RenderGraphPlan::StepKind::Transition;
            for (auto& update : batch.plan.m_BufferUpdates) {
                RHIBufferStateSnapshot current;
                if (const auto s = device->GetBufferState(update.buffer, current); !s.IsSuccess())
                    return s;
                if (const auto s = frontier(batch, current.state, update.writes); !s.IsSuccess())
                    return s;
                if (current.state.revision == UINT64_MAX)
                    return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/RevisionExhausted"};
                update.expectedRevision = current.state.revision;
                update.after.revision = current.state.revision + 1;
                update.after.ownership = {};
                update.after.ownership.owner = batch.queue->GetId();
                if (current.state.ownership.IsReleased()) {
                    const RHIBufferAccess access{RHIPipelineStage::AllCommands,
                                                 RHIResourceAccess::MemoryReadWrite};
                    pre.bufferBarriers.push_back(
                        {update.buffer,
                         0,
                         current.desc.size,
                         access,
                         access,
                         {current.state.ownership.owner, batch.queue->GetId(), RHIOwnershipPhase::Acquire}});
                }
            }
            for (auto& update : batch.plan.m_TextureUpdates) {
                RHITextureStateSnapshot current;
                if (const auto s = device->GetTextureState(update.view, current); !s.IsSuccess())
                    return s;
                const RHITextureSubresourceRange range{.levelCount = current.mipLevels,
                                                       .layerCount = current.arrayLayers,
                                                       .aspect = GetPixelFormatInfo(current.format).isDepth
                                                                     ? RHITextureAspect::Depth
                                                                     : RHITextureAspect::Color};
                if (current.state.revision == UINT64_MAX)
                    return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/RevisionExhausted"};
                update.expectedRevision = current.state.revision;
                update.after.revision = current.state.revision + 1;
                update.after.ownership = {};
                update.after.ownership.owner = batch.queue->GetId();
                if (current.state.ownership.IsReleased()) {
                    auto before = current.state.access, after = current.state.access;
                    before.layout = current.state.ownership.releaseOldLayout;
                    after.layout = current.state.ownership.releaseNewLayout;
                    after.stage = RHIPipelineStage::AllCommands;
                    after.access = RHIResourceAccess::None;
                    pre.textureBarriers.push_back(
                        {.view = update.view,
                         .range = range,
                         .before = before,
                         .after = after,
                         .transfer = {current.state.ownership.owner, batch.queue->GetId(),
                                      RHIOwnershipPhase::Acquire}});
                }
                // 批次起点来自已接受状态；首个使用即使没有编译期屏障，也要建立可见性。
                RHITextureAccess firstAccess = current.state.access;
                for (const auto& step : batch.plan.m_Steps) {
                    const auto found = std::find_if(step.textureStates.begin(), step.textureStates.end(),
                                                    [&](const auto& u) { return u.view == update.view; });
                    if (found != step.textureStates.end()) {
                        firstAccess = found->after.access;
                        break;
                    }
                }
                const auto initial = current.state.access;
                visibility.textureBarriers.push_back(
                    {.view = update.view, .range = range, .before = initial, .after = firstAccess});
                bool changesLayout = initial.layout != firstAccess.layout;
                auto previousAccess = firstAccess;

                // WSI下沉会把离屏导出移到UI采样之前。必须沿重排后的每一步重算旧布局，
                // 否则原先的Read->Read导出不能转换实际的ColorAttachment，却会发布可采样状态。
                for (auto& step : batch.plan.m_Steps) {
                    const auto required = std::find_if(
                        step.textureStates.begin(), step.textureStates.end(),
                        [&](const auto& state) { return state.view == update.view; });
                    if (required == step.textureStates.end())
                        continue;

                    bool hasBarrier = false;
                    for (auto& barrier : step.textureBarriers)
                        if (barrier.view == update.view) {
                            // 访问类型必须与实际旧布局一致，源阶段仍保守覆盖此前所有命令。
                            barrier.before = previousAccess;
                            barrier.before.stage = RHIPipelineStage::AllCommands;
                            changesLayout |= previousAccess.layout != barrier.after.layout;
                            previousAccess = barrier.after;
                            hasBarrier = true;
                        }
                    const auto after = required->after.access;
                    if (previousAccess.stage != after.stage || previousAccess.access != after.access ||
                        previousAccess.layout != after.layout) {
                        if (hasBarrier)
                            return {RHIErrorCode::InvalidState, 0, "FrameScheduler/TextureStepStateMismatch"};
                        // 编译时连续的只读访问可能省略屏障，拆批/重排后重新检查。
                        step.textureBarriers.push_back(
                            {.view = update.view,
                             .range = range,
                             .before = {RHIPipelineStage::AllCommands, previousAccess.access,
                                        previousAccess.layout},
                             .after = after});
                        changesLayout |= previousAccess.layout != after.layout;
                        previousAccess = after;
                    }
                }
                // 中途改变再恢复layout也属于独占状态写，不能只比较批次首尾。
                update.writes = update.writes || changesLayout;
                if (const auto s = frontier(batch, current.state, update.writes); !s.IsSuccess())
                    return s;
            }
            if (!visibility.textureBarriers.empty())
                batch.plan.m_Steps.insert(batch.plan.m_Steps.begin(), std::move(visibility));
            if (!pre.bufferBarriers.empty() || !pre.textureBarriers.empty())
                batch.plan.m_Steps.insert(batch.plan.m_Steps.begin(), std::move(pre));
            if (batch.presentation) {
                // 最后时间戳与RenderFinished覆盖每个已接受queue，不使用某一queue的假全序。
                for (auto point : result.completions.GetPoints())
                    if (const auto s = include(batch, point); !s.IsSuccess())
                        return s;
                batch.plan.m_Queries = plan.m_Queries;
                batch.plan.m_QueryEnd = true;
                batch.plan.m_QueryBegin = false;
            }
            return prepareCommands(batch);
        };
        const auto independent = [&](const Batch& first, const Batch& second) {
            for (const auto& a : first.plan.m_BufferUpdates)
                for (const auto& b : second.plan.m_BufferUpdates)
                    if (a.buffer == b.buffer)
                        return false;
            for (const auto& a : first.plan.m_TextureUpdates)
                for (const auto& b : second.plan.m_TextureUpdates)
                    if (a.view == b.view)
                        return false;
            if (ancestors[first.index][second.index] || ancestors[second.index][first.index])
                return false;
            return !first.presentation && !second.presentation;
        };
        for (size_t first = 0; first < batches.size();) {
            size_t end = first + 1;
            if (asyncQueues && jobs && jobs->IsRunning())
                for (; end < batches.size() && end - first < 4; ++end) {
                    bool disjoint = true;
                    for (size_t i = first; i < end; ++i)
                        if (!independent(*batches[i], *batches[end]))
                            disjoint = false;
                    if (!disjoint)
                        break;
                }
            // host先准备独立pool、完整状态证明和依赖，再交唯一Jobs并行录制；join前不提交/创建/释放GPU资源。
            for (size_t i = first; i < end; ++i)
                if (const auto s = bind(*batches[i]); !s.IsSuccess())
                    return s;
            std::array<JobHandle, 4> handles;
            size_t launched = 0;
            struct RecordingJoin
            {
                JobSystem* jobs;
                std::array<JobHandle, 4>& handles;
                size_t& launched;
                ~RecordingJoin() noexcept
                {
                    // 即使Submit/Wait复制诊断时分配失败，也先结束所有借用batch的worker。
                    for (size_t i = 0; i < launched; ++i)
                        handles[i].Cancel();
                    for (size_t i = 0; i < launched; ++i)
                        while (!jobs->IsComplete(handles[i]))
                            std::this_thread::yield();
                }
            } joined{jobs, handles, launched};
            std::string jobError;
            if (end - first > 1) {
                for (size_t i = first; i < end; ++i) {
                    auto* batch = batches[i].get();
                    if (!jobs->Submit(
                            [batch](const CancellationToken& token) {
                                if (token.IsCancellationRequested()) {
                                    batch->recorded = {RHIErrorCode::InvalidState, 0,
                                                       "FrameScheduler/RecordingCancelled"};
                                    return;
                                }
                                batch->recorded = batch->plan.Record(*batch->commands.commands);
                            },
                            handles[launched], jobError))
                        break;
                    ++launched;
                }
                for (size_t i = 0; i < launched; ++i) {
                    std::string error;
                    if (!jobs->Wait(handles[i], error))
                        batches[first + i]->recorded = {RHIErrorCode::BackendFailure, 0,
                                                        "FrameScheduler/RecordingWorkerFailed"};
                }
                jobs->CollectCompleted();
                result.parallelRecordedBatchCount += static_cast<uint32_t>(launched);
            }
            for (size_t i = first + launched; i < end; ++i)
                batches[i]->recorded = batches[i]->plan.Record(*batches[i]->commands.commands);
            for (size_t i = first; i < end; ++i) {
                if (!batches[i]->recorded.IsSuccess())
                    return batches[i]->recorded;
                if (const auto status = submit(*batches[i]); !status.IsSuccess())
                    return status;
            }
            first = end;
        }
        return {};
    }
    catch (const std::bad_alloc&) {
        return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/Allocation"};
    }
    catch (const std::length_error&) {
        return {RHIErrorCode::OutOfMemory, 0, "FrameScheduler/Capacity"};
    }
} // namespace Catalpa
