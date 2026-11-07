#include "catpch.h"
#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/ResourceResidencyManager.h"
#include "Core/Jobs/JobSystem.h"

namespace Catalpa::Tests {
    namespace {
        class ScheduledDevice;
        class ScheduledQueue final : public RHIQueue {
          public:
            ScheduledDevice* owner = nullptr;
            QueueId id;
            uint64_t sequence = 0;
            QueueId GetId() const override {
                return id;
            }
            RHIDevice* GetDevice() noexcept override;
            RHIStatus Submit(const RHISubmission&, RHICompletionPoint&) override;
            RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override {
                complete = false;
                return point.queue == id && point.sequence <= sequence
                           ? RHIStatus{}
                           : RHIStatus{RHIErrorCode::InvalidDescription, 0, "ScheduledFake/UnknownPoint"};
            }
        };
        // 仅驱动生产Scheduler/Compiler的已接受状态协议，不模拟GPU执行/驱动内存释放。
        class ScheduledDevice final : public FakeDevice {
          public:
            std::array<ScheduledQueue, 3> queues;
            std::vector<std::unique_ptr<FakeCommandList>> commands;
            uint32_t releases = 0, acquires = 0, created = 0, ownerReleases = 0;
            QueueId failQueue;
            bool scheduledBatches = true;
            bool verifyTextureLayouts = false;
            std::vector<RHITextureLayout> submittedTextureLayouts;
            size_t sampledTextureChecks = 0;
            ScheduledDevice() {
                for (uint32_t i = 0; i < 3; ++i) {
                    queues[i].id = {i + 1};
                    queues[i].owner = this;
                }
            }
            RHIQueue& GetGraphicsQueue() override {
                return queues[0];
            }
            RHIQueue* GetQueue(QueueId id) override {
                return id.value >= 1 && id.value <= 3 ? &queues[id.value - 1] : nullptr;
            }
            RHIQueue& GetQueue(RHIQueueClass type) override {
                return queues[type == RHIQueueClass::Graphics ? 0 : type == RHIQueueClass::Compute ? 1 : 2];
            }
            RHIQueueInfo GetQueueInfo(QueueId id) const override {
                return id.value >= 1 && id.value <= 3
                           ? RHIQueueInfo{id, id.value - 1, id.value == 1, id.value <= 2, true}
                           : RHIQueueInfo{};
            }
            bool SupportsScheduledBatches() const noexcept override {
                return scheduledBatches;
            }
            RHIStatus PrepareAuxiliaryCommands(RHIFrameToken frame, QueueId, uint32_t ordinal,
                                               RHIFrameResources& out) override {
                if (ordinal == 0)
                    commands.clear();
                if (ordinal != commands.size())
                    return {RHIErrorCode::InvalidState, 0, "ScheduledFake/Ordinal"};
                commands.push_back(std::make_unique<FakeCommandList>());
                commands.back()->recordTextureLayouts = verifyTextureLayouts;
                out = {frame, commands.back().get()};
                return {};
            }
            // 独立回放已记录命令，不用textureUpdates推导实际布局；仅支持本用例的单mip/layer。
            RHIStatus ReplayTextureLayouts(const FakeCommandList& list,
                                          std::vector<RHITextureLayout>& layouts, size_t& sampled) const {
                if (!list.recordTextureLayouts)
                    return {RHIErrorCode::InvalidState, 0, "ScheduledFake/TextureCommandsNotRecorded"};
                layouts = submittedTextureLayouts;
                for (size_t i = layouts.size(); i < textures.size(); ++i)
                    layouts.push_back(textures[i].state.access.layout);
                sampled = 0;

                const auto validView = [&](TextureViewHandle view) {
                    return view.device == GetDeviceId() && view.slot.generation == 1 &&
                           view.slot.index < textures.size() && textures[view.slot.index].mipLevels == 1 &&
                           textures[view.slot.index].arrayLayers == 1;
                };
                for (const auto& command : list.textureCommands) {
                    if (command.kind == FakeCommandList::TextureCommand::Kind::RequireLayout) {
                        if (!validView(command.view) || layouts[command.view.slot.index] != command.layout)
                            return {RHIErrorCode::InvalidState, 0, "ScheduledFake/TextureUseLayout"};
                        if (command.layout == RHITextureLayout::ShaderReadOnly)
                            ++sampled;
                        continue;
                    }

                    const auto& barrier = command.barrier;
                    if (!validView(barrier.view) || barrier.range.baseMipLevel != 0 ||
                        barrier.range.levelCount != 1 || barrier.range.baseArrayLayer != 0 ||
                        barrier.range.layerCount != 1 || barrier.transfer.phase != RHIOwnershipPhase::None)
                        return {RHIErrorCode::Unsupported, 0, "ScheduledFake/TextureLayoutFixtureRange"};
                    // Synchronization2同队列oldLayout==newLayout只提供同步，不会把图像改成该布局。
                    // 不能将陈旧的Read->Read当作ColorAttachment->Read，否则替身也会漏掉原始错误。
                    if (barrier.before.layout == barrier.after.layout)
                        continue;
                    auto& actual = layouts[barrier.view.slot.index];
                    if (barrier.before.layout != RHITextureLayout::Undefined &&
                        actual != barrier.before.layout)
                        return {RHIErrorCode::InvalidState, 0, "ScheduledFake/TextureBarrierOldLayout"};
                    actual = barrier.after.layout;
                }

                for (const auto& update : list.textureUpdates)
                    if (!validView(update.view) || layouts[update.view.slot.index] != update.after.access.layout)
                        return {RHIErrorCode::InvalidState, 0, "ScheduledFake/TexturePublicationLayout"};
                return {};
            }
            RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& out) override {
                RHIBufferStateSnapshot value;
                value.desc = desc;
                out = AddBuffer(value);
                ++created;
                return {};
            }
            void ReleaseBuffer(BufferHandle handle) override {
                if (handle.HasValue())
                    ++ownerReleases;
            }
        };
        RHIDevice* ScheduledQueue::GetDevice() noexcept {
            return owner;
        }
        RHIStatus ScheduledQueue::Submit(const RHISubmission& submission, RHICompletionPoint& point) {
            point = {};
            if (owner->failQueue == id)
                return {RHIErrorCode::OutOfMemory, 0, "ScheduledFake/InjectedSubmitFailure"};
            auto* list = dynamic_cast<FakeCommandList*>(submission.commands);
            if (!list || list->state != RHICommandListState::Executable)
                return {RHIErrorCode::InvalidState, 0, "ScheduledFake/NotExecutable"};
            const auto waits = [&](RHICompletionPoint needed) {
                return std::any_of(submission.dependencies.begin(), submission.dependencies.end(),
                                   [&](auto wait) {
                                       return wait.device == needed.device && wait.queue == needed.queue &&
                                              wait.sequence >= needed.sequence;
                                   });
            };
            auto buffers = list->bufferUpdates;
            auto textures = list->textureUpdates;
            for (auto& update : buffers) {
                const auto& before = owner->buffers[update.buffer.slot.index].state;
                if (before.revision != update.expectedRevision)
                    return {RHIErrorCode::InvalidState, 0, "ScheduledFake/BufferRevision"};
                if (before.ownership.IsReleased() && !update.after.ownership.IsReleased() &&
                    !waits(before.ownership.releasePoint))
                    return {RHIErrorCode::InvalidState, 0, "ScheduledFake/MissingAcquireWait"};
                if (const auto status = update.after.frontier.readers.Reserve(4); !status.IsSuccess())
                    return status;
            }
            for (auto& update : textures)
                if (owner->textures[update.view.slot.index].state.revision != update.expectedRevision)
                    return {RHIErrorCode::InvalidState, 0, "ScheduledFake/TextureRevision"};
            std::vector<RHITextureLayout> layouts;
            size_t sampled = 0;
            if (owner->verifyTextureLayouts)
                if (const auto status = owner->ReplayTextureLayouts(*list, layouts, sampled); !status.IsSuccess())
                    return status;
            point = {owner->GetDeviceId(), id, ++sequence};
            list->state = RHICommandListState::Pending;
            if (owner->verifyTextureLayouts) {
                owner->submittedTextureLayouts.swap(layouts);
                owner->sampledTextureChecks += sampled;
            }
            for (auto& update : buffers) {
                auto& state = owner->buffers[update.buffer.slot.index].state;
                if (update.after.ownership.IsReleased()) {
                    ++owner->releases;
                    update.after.ownership.releasePoint = point;
                } else {
                    if (state.ownership.IsReleased())
                        ++owner->acquires;
                    update.after.ownership.owner = id;
                }
                update.after.lastUse = point;
                if (update.writes) {
                    update.after.frontier.writer = point;
                    update.after.frontier.readers.Clear();
                } else
                    (void)update.after.frontier.readers.TryInclude(point);
                state = std::move(update.after);
            }
            for (auto& update : textures) {
                update.after.lastUse = point;
                update.after.ownership.owner = id;
                owner->textures[update.view.slot.index].state = std::move(update.after);
            }
            return {};
        }
        TextureViewHandle PresentImage(ScheduledDevice& device) {
            RHITextureStateSnapshot state;
            state.extent = {16, 16};
            state.format = PixelFormat::RGBA8_UNorm;
            state.usage = RHITextureUsage::ColorAttachment;
            state.presentable = true;
            return device.AddTexture(state);
        }
        bool EndGraph(RenderGraph& graph, TextureViewHandle image) {
            RGTexture target;
            return graph.ImportTexture(image, target).IsSuccess() &&
                   graph.AddClearPass("WSI clear", target, {0, 0, 0, 1}).IsSuccess() &&
                   graph.ExportPresent(target).IsSuccess();
        }
        RHISubmission Submission(ScheduledDevice& device, FakeCommandList& commands,
                                 uint64_t generation = 1) {
            const RHIFrameToken frame{device.GetDeviceId(), 0, generation};
            return {.frame = frame,
                    .commands = &commands,
                    .wait = {frame, RHIGpuDependencyKind::AcquireReady},
                    .signal = {frame, RHIGpuDependencyKind::RenderFinished}};
        }
    } // namespace
    void RunAdvancedSchedulingTests(TestContext& test) {
        ScheduledDevice device;
        RHIBufferStateSnapshot input;
        input.desc = {16, RHIBufferUsage::TransferSource, RHIMemoryDomain::Upload};
        input.state.initialized = {{0, 16}};
        input.state.ownership.owner = {1};
        const auto source = device.AddBuffer(input);
        RHIBufferStateSnapshot output;
        output.desc = {16, RHIBufferUsage::TransferDestination, RHIMemoryDomain::Readback};
        const auto destination = device.AddBuffer(output);
        const auto image = PresentImage(device);
        auto makePlan = [&](RenderGraphPlan& plan) {
            RenderGraph graph(device);
            RGBuffer a, b;
            return graph.ImportBuffer(source, a).IsSuccess() &&
                   graph.ImportBuffer(destination, b).IsSuccess() &&
                   graph.AddCopyPass("Copy and host visibility", a, b, {0, 0, 16}).IsSuccess() &&
                   EndGraph(graph, image) && graph.Compile(plan).IsSuccess();
        };
        RenderGraphPlan plan;
        test.Check(makePlan(plan),
                   "Scheduled graph retains transfer copy, explicit HostRead and one WSI output");
        FakeCommandList failedMain;
        device.failQueue = {3};
        FrameSubmissionResult partial;
        const auto failed =
            FrameScheduler::Execute(plan, device.GetGraphicsQueue(), Submission(device, failedMain), partial);
        test.Check(
            !failed.IsSuccess() && partial.anyAccepted && !partial.accepted &&
                partial.acceptedBatchCount == 1 && partial.completions.GetPoints().size() == 1,
            "A transfer-submit failure preserves the already accepted graphics release without inventing "
            "presentation acceptance");
        test.Check(device.buffers[source.slot.index].state.ownership.IsReleased() &&
                       device.buffers[source.slot.index].state.ownership.releasePoint.HasValue() &&
                       device.buffers[destination.slot.index].state.initialized.empty(),
                   "Accepted release has a real point while rejected copy publishes no initialized bytes");
        device.failQueue = {};
        RenderGraphPlan retry;
        test.Check(makePlan(retry), "A fresh plan can import the pending accepted ownership release");
        FakeCommandList main;
        FrameSubmissionResult completed;
        test.Check(
            FrameScheduler::Execute(retry, device.GetGraphicsQueue(), Submission(device, main, 2), completed)
                    .IsSuccess() &&
                completed.accepted && completed.anyAccepted && completed.completions.GetPoints().size() == 2,
            "Retry pairs the accepted release with a dependent transfer acquire and submits WSI exactly "
            "once");
        const auto& copied = device.buffers[destination.slot.index].state;
        test.Check(device.acquires == 1 && !device.buffers[source.slot.index].state.ownership.IsReleased() &&
                       copied.access.access == RHIResourceAccess::HostRead &&
                       copied.lastUse.queue == QueueId{3},
                   "Readback visibility and completion keep their actual transfer queue identity");
        RenderGraphPlan serialPlan;
        FakeCommandList serialCommands;
        FrameSubmissionResult serial;
        test.Check(
            makePlan(serialPlan) &&
                FrameScheduler::Execute(serialPlan, device.GetGraphicsQueue(),
                                        Submission(device, serialCommands, 3), serial, nullptr, false)
                    .IsSuccess() &&
                serial.accepted && device.buffers[destination.slot.index].state.lastUse.queue == QueueId{1},
            "Serial comparison migrates prior owners safely and records the same graph payload on graphics");
        auto alias = [&](bool overlap) {
            ScheduledDevice local;
            const auto present = PresentImage(local);
            RHIBufferStateSnapshot s = input;
            s.state.ownership = {};
            const auto upload = local.AddBuffer(s);
            const auto aOut = local.AddBuffer(output), bOut = local.AddBuffer(output);
            RenderGraph graph(local);
            RGBuffer a, b, src, outA, outB;
            const RHIBufferDesc desc{16, RHIBufferUsage::TransferSource | RHIBufferUsage::TransferDestination,
                                     RHIMemoryDomain::Device};
            bool valid = graph.CreateTransientBuffer(desc, a).IsSuccess() &&
                         graph.CreateTransientBuffer(desc, b).IsSuccess() &&
                         graph.ImportBuffer(upload, src).IsSuccess() &&
                         graph.ImportBuffer(aOut, outA).IsSuccess() &&
                         graph.ImportBuffer(bOut, outB).IsSuccess();
            valid = valid && graph.AddCopyPass("Produce A", src, a, {0, 0, 16}).IsSuccess();
            const auto first = graph.GetLastPass();
            if (overlap) {
                valid = valid && graph.AddCopyPass("Produce B", src, b, {0, 0, 16}).IsSuccess();
                const auto second = graph.GetLastPass();
                valid = valid && graph.AddCopyPass("Consume A", a, outA, {0, 0, 16}).IsSuccess();
                const auto third = graph.GetLastPass();
                valid = valid && graph.AddDependency(first, second).IsSuccess() &&
                        graph.AddDependency(second, third).IsSuccess();
            } else
                valid = valid && graph.AddCopyPass("Consume A", a, outA, {0, 0, 16}).IsSuccess() &&
                        graph.AddCopyPass("Produce B", src, b, {0, 0, 16}).IsSuccess();
            valid = valid && graph.AddCopyPass("Consume B", b, outB, {0, 0, 16}).IsSuccess() &&
                    EndGraph(graph, present);
            RenderGraphPlan compiled;
            valid = valid && graph.Compile(compiled).IsSuccess();
            test.Check(
                valid && compiled.GetSchedule().transientAllocationCount == (overlap ? 2u : 1u) &&
                    compiled.GetSchedule().aliasedLogicalBufferCount == (overlap ? 0u : 1u),
                overlap ? "Overlapping lifetimes cannot alias despite identical buffer descriptions"
                        : "Compatible transient lifetimes share one real allocation only after explicit DAG "
                          "ordering is installed");
            if (valid) {
                FakeCommandList recorded;
                test.Check(
                    compiled.Record(recorded).IsSuccess(),
                    "Aliased logical buffers record through the production compiler and command plan without "
                    "duplicate physical state transactions");
            }
        };
        alias(false);
        alias(true);
        {
            ScheduledDevice manyUses;
            auto sourceState = input;
            sourceState.state.ownership = {};
            auto targetState = output;
            targetState.desc.memory = RHIMemoryDomain::Device;
            const auto upload = manyUses.AddBuffer(sourceState), readback = manyUses.AddBuffer(targetState);
            const auto present = PresentImage(manyUses);
            RenderGraph graph(manyUses);
            RGBuffer sourceBuffer, target, a, b;
            const RHIBufferDesc desc{16, RHIBufferUsage::TransferSource | RHIBufferUsage::TransferDestination,
                                    RHIMemoryDomain::Device};
            bool valid = graph.ImportBuffer(upload, sourceBuffer).IsSuccess() &&
                         graph.ImportBuffer(readback, target).IsSuccess() &&
                         graph.CreateTransientBuffer(desc, a).IsSuccess() &&
                         graph.CreateTransientBuffer(desc, b).IsSuccess() &&
                         graph.AddCopyPass("Produce many-use A", sourceBuffer, a, {0, 0, 16}).IsSuccess();
            constexpr uint32_t readerCount = 2048;
            for (uint32_t i = 0; valid && i < readerCount; ++i)
                valid = graph.AddCopyPass("Read many-use A", a, target, {0, 0, 16}).IsSuccess();
            valid = valid && graph.AddCopyPass("Produce many-use B", sourceBuffer, b, {0, 0, 16}).IsSuccess();
            for (uint32_t i = 0; valid && i < readerCount; ++i)
                valid = graph.AddCopyPass("Read many-use B", b, target, {0, 0, 16}).IsSuccess();
            RenderGraphPlan compiled;
            valid = valid && EndGraph(graph, present) && graph.Compile(compiled).IsSuccess();
            test.Check(valid && compiled.GetSchedule().transientAllocationCount == 1 &&
                           compiled.GetSchedule().aliasedLogicalBufferCount == 1 &&
                           compiled.GetSchedule().dependencies.size() < size_t(readerCount) * 6 + 32,
                       "Thousands of transient consumers alias through DAG entry/exit frontiers without "
                       "a quadratic cross-product of dependency edges");
        }
        {
            // 覆盖合法16384粒子的draw/pick与多视图量级；验证真实调度而非仅放宽常量。
            ScheduledDevice large;
            auto sourceState = input;
            sourceState.state.ownership = {};
            auto targetState = output;
            targetState.desc.memory = RHIMemoryDomain::Device;
            const auto upload = large.AddBuffer(sourceState), target = large.AddBuffer(targetState);
            const auto present = PresentImage(large);
            RenderGraph graph(large);
            RGBuffer a, b;
            bool valid =
                graph.ImportBuffer(upload, a).IsSuccess() && graph.ImportBuffer(target, b).IsSuccess();
            constexpr uint32_t passCount = 65536;
            for (uint32_t i = 0; valid && i < passCount; ++i)
                valid = graph.AddCopyPass("Large ordered transfer scope", a, b, {0, 0, 16}).IsSuccess();
            RenderGraphPlan compiled;
            valid = valid && EndGraph(graph, present) && graph.Compile(compiled).IsSuccess();
            FakeCommandList primary;
            FrameSubmissionResult execution;
            test.Check(valid && compiled.GetSchedule().passOrder.size() == size_t(passCount) + 1 &&
                           FrameScheduler::Execute(compiled, large.GetGraphicsQueue(),
                                                   Submission(large, primary), execution)
                               .IsSuccess() &&
                           execution.accepted && execution.acceptedBatchCount == 2 &&
                           large.commands.size() == 1 && large.commands.front()->copyCount == passCount,
                       "65536 live passes use sparse dependency proof and one continuous transfer batch, "
                       "rather than one context per fixed step chunk");
        }
        {
            ScheduledDevice grouped;
            RenderGraph graph(grouped);
            constexpr uint32_t resourceCount = 300;
            bool valid = true;
            for (uint32_t i = 0; valid && i < resourceCount; ++i) {
                const auto upload = grouped.AddBuffer(input), target = grouped.AddBuffer(output);
                RGBuffer a, b;
                valid = graph.ImportBuffer(upload, a).IsSuccess() &&
                        graph.ImportBuffer(target, b).IsSuccess() &&
                        graph.AddCopyPass("Grouped ownership transfer", a, b, {0, 0, 16}).IsSuccess();
            }
            const auto present = PresentImage(grouped);
            RenderGraphPlan compiled;
            valid = valid && EndGraph(graph, present) && graph.Compile(compiled).IsSuccess();
            FakeCommandList primary;
            FrameSubmissionResult execution;
            test.Check(valid &&
                           FrameScheduler::Execute(compiled, grouped.GetGraphicsQueue(),
                                                   Submission(grouped, primary), execution)
                               .IsSuccess() &&
                           execution.accepted && grouped.releases == resourceCount &&
                           grouped.acquires == resourceCount && execution.acceptedBatchCount == 3 &&
                           grouped.commands.size() == 2,
                       "Hundreds of resource ownership releases share one queue batch without consuming "
                       "hundreds of auxiliary pools");
        }
        {
            ScheduledDevice switching;
            RenderGraph graph(switching);
            bool valid = true;
            for (uint32_t i = 0; valid && i < 129; ++i) {
                auto initial = input;
                initial.state.ownership = {};
                const auto upload = switching.AddBuffer(initial), target = switching.AddBuffer(output);
                RGBuffer a, b;
                RHITextureStateSnapshot imageState;
                imageState.extent = {16, 16};
                imageState.format = PixelFormat::RGBA8_UNorm;
                imageState.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
                RGTexture image;
                valid = graph.ImportBuffer(upload, a).IsSuccess() &&
                        graph.ImportBuffer(target, b).IsSuccess() &&
                        graph.AddCopyPass("Alternating queue copy", a, b, {0, 0, 16}).IsSuccess() &&
                        graph.ImportTexture(switching.AddTexture(imageState), image).IsSuccess() &&
                        graph.AddClearPass("Alternating graphics clear", image, {0, 0, 0, 1}).IsSuccess() &&
                        graph.ExportTexture(image).IsSuccess();
            }
            const auto present = PresentImage(switching);
            RenderGraphPlan compiled;
            valid = valid && EndGraph(graph, present) && graph.Compile(compiled).IsSuccess();
            FakeCommandList primary;
            FrameSubmissionResult execution;
            test.Check(valid &&
                           FrameScheduler::Execute(compiled, switching.GetGraphicsQueue(),
                                                   Submission(switching, primary), execution)
                               .IsSuccess() &&
                           execution.accepted && execution.serializedForBudget &&
                           execution.acceptedBatchCount == 2,
                       "Excessive queue switches select a visible ordered graphics fallback instead of "
                       "failing an otherwise valid frame");
        }
        {
            ScheduledDevice concurrent;
            JobSystem jobs;
            std::string error;
            const bool running = jobs.Start({2, 16}, error);
            test.Check(running, "Parallel recording borrows one existing bounded JobSystem");
            if (running) {
                auto initial = input;
                initial.state.ownership = {};
                const auto firstSource = concurrent.AddBuffer(initial),
                           secondSource = concurrent.AddBuffer(initial);
                const auto firstOutput = concurrent.AddBuffer(output),
                           secondOutput = concurrent.AddBuffer(output);
                const auto present = PresentImage(concurrent);
                RHITextureStateSnapshot offscreen;
                offscreen.extent = {16, 16};
                offscreen.format = PixelFormat::RGBA8_UNorm;
                offscreen.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
                const auto view = concurrent.AddTexture(offscreen);
                RenderGraph graph(concurrent);
                RGBuffer a, b, c, d;
                RGTexture target;
                bool valid = graph.ImportBuffer(firstSource, a).IsSuccess() &&
                             graph.ImportBuffer(firstOutput, b).IsSuccess() &&
                             graph.ImportBuffer(secondSource, c).IsSuccess() &&
                             graph.ImportBuffer(secondOutput, d).IsSuccess() &&
                             graph.ImportTexture(view, target).IsSuccess();
                valid = valid && graph.AddCopyPass("Independent transfer A", a, b, {0, 0, 16}).IsSuccess() &&
                        graph.AddClearPass("Independent offscreen clear", target, {0, 0, 0, 1}).IsSuccess() &&
                        graph.AddCopyPass("Independent transfer B", c, d, {0, 0, 16}).IsSuccess() &&
                        graph.ExportTexture(target).IsSuccess() && EndGraph(graph, present);
                RenderGraphPlan compiled;
                valid = valid && graph.Compile(compiled).IsSuccess();
                FakeCommandList primary;
                FrameSubmissionResult execution;
                test.Check(
                    valid &&
                        FrameScheduler::Execute(compiled, concurrent.GetGraphicsQueue(),
                                                Submission(concurrent, primary), execution, &jobs)
                            .IsSuccess() &&
                        execution.accepted && execution.parallelRecordedBatchCount >= 2,
                    "Disjoint transfer/graphics batches record on existing workers, join, and submit with "
                    "separate state snapshots");
                jobs.Shutdown();
            }
        }

        // 离屏输出既被同图WSI绘制采样又被导出；WSI下沉会把导出移到采样之前。
        // 线性路径作对照，分批路径分别关闭/开启异步队列，并复用同一图像检查下一帧。
        const std::array<std::string_view, 3> layoutModes{"linear", "scheduled serial", "scheduled async"};
        for (size_t mode = 0; mode < layoutModes.size(); ++mode) {
            ScheduledDevice preview;
            preview.scheduledBatches = mode != 0;
            preview.verifyTextureLayouts = true;
            const auto present = PresentImage(preview);
            RHITextureStateSnapshot offscreen;
            offscreen.extent = {16, 16};
            offscreen.format = PixelFormat::RGBA8_UNorm;
            offscreen.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
            const auto view = preview.AddTexture(offscreen);

            for (uint64_t frame = 1; frame <= 2; ++frame) {
                RenderGraph graph(preview);
                RGTexture target, sampled;
                bool valid = graph.ImportTexture(present, target).IsSuccess() &&
                             graph.ImportTexture(view, sampled).IsSuccess() &&
                             graph.AddClearPass("Offscreen preview", sampled, {0, 0, 0, 1}).IsSuccess();
                RGIndexedDrawDesc draw;
                draw.color = target;
                draw.pipeline = {preview.GetDeviceId(), {0, 1}};
                draw.vertexCount = 3;
                draw.viewport = {0, 0, 16, 16, 0, 1};
                draw.scissor.extent = {16, 16};
                draw.sampledTextures = {{sampled, SamplerHandle{preview.GetDeviceId(), {0, 1}}, 0, 1}};
                valid = valid && graph.AddDrawPass("ImGui preview sample", draw).IsSuccess() &&
                        graph.ExportTexture(sampled).IsSuccess() && graph.ExportPresent(target).IsSuccess();
                RenderGraphPlan compiled;
                valid = valid && graph.Compile(compiled).IsSuccess();
                const std::string label = std::string(layoutModes[mode]) + " preview frame " + std::to_string(frame);
                test.Check(valid, label + " compiles a shared offscreen export and WSI sampled consumer");
                if (!valid)
                    break;

                FakeCommandList primary;
                primary.recordTextureLayouts = true;
                FrameSubmissionResult execution;
                const auto status = FrameScheduler::Execute(compiled, preview.GetGraphicsQueue(),
                    Submission(preview, primary, frame), execution, nullptr, mode == 2);
                test.Check(status.IsSuccess() && execution.accepted &&
                               execution.acceptedBatchCount == (mode == 0 ? 1 : 2),
                           label + " records valid attachment/sample layouts through every submitted batch");
                if (!status.IsSuccess())
                    break;
                test.Check(preview.sampledTextureChecks == frame &&
                               preview.submittedTextureLayouts[view.slot.index] == RHITextureLayout::ShaderReadOnly &&
                               preview.textures[view.slot.index].state.access.layout == RHITextureLayout::ShaderReadOnly &&
                               preview.submittedTextureLayouts[present.slot.index] == RHITextureLayout::Present,
                           label + " publishes the actual sampled export and presentation layouts");
            }
        }

        ResourceResidencyManager residency(64, 1);
        const ResourceResidencyKey key{AssetId::FromWords(0x54455354, 1), 1, 0};
        bool evicted = false;
        test.Check(residency.Track(
                                key, 16, [] { return true; }, [&] { evicted = true; })
                       .IsSuccess(),
                   "Residency allocates completion bookkeeping before a submission can be accepted");
        const std::array uses{RHICompletionPoint{{11}, {1}, 2}, RHICompletionPoint{{11}, {3}, 7}};
        test.Check(residency.Touch(key, uses), "Residency retains distinct graphics and transfer frontiers");
        bool transferDone = false;
        const auto query = [&](const RHICompletionPoint& point, bool& complete) {
            complete = point.queue == QueueId{1} || transferDone;
            return RHIStatus{};
        };
        test.Check(!residency.MakeRoom(8, query).IsSuccess() && !evicted && residency.Size() == 1,
                   "An entry limit cannot evict a payload while any queue is incomplete");
        transferDone = true;
        test.Check(residency.MakeRoom(8, query).IsSuccess() && evicted && residency.Size() == 0,
                   "Completed LRU can free an entry slot even when the payload byte budget already has room");

        // 失效缓存仍可能被Undo重新取得；在途期间不能移除owner再重建同一版本的账目。
        bool pinned = false;
        evicted = false;
        transferDone = false;
        test.Check(residency.Track(key, 16, [&] { return !pinned; }, [&] { evicted = true; }).IsSuccess() &&
                       residency.Touch(key, uses),
                   "An invalidation fixture retains its original owner and accepted queue uses");
        residency.RequestEviction(key);
        test.Check(residency.MakeRoom(0, query).IsSuccess() && !evicted && residency.Size() == 1 &&
                       residency.Touch(key, {}),
                   "Invalidation leaves an in-flight version reusable without a duplicate Track");
        transferDone = true;
        pinned = true;
        test.Check(residency.MakeRoom(0, query).IsSuccess() && !evicted && residency.Budget().used == 16,
                   "An old version reacquired by Undo stays resident while its lease is pinned");
        pinned = false;
        test.Check(residency.MakeRoom(0, query).IsSuccess() && evicted && residency.Budget().used == 0,
                   "Unused invalidated versions retire after all queues complete without budget pressure");
        test.Check(residency.Track(key, 16, [] { return true; }, [] {}).IsSuccess(),
                   "A fully retired exact version can be recreated without a residency key collision");
    }
} // namespace Catalpa::Tests
