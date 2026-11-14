#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa::Tests {
	namespace {
		/**
		 * @brief 建立真实Graph/Compiler/Executor路径；仅设备状态来源是CPU替身。
		 *
		 * @param[in,out] device 提供帧计划所需资源状态的 CPU 测试设备。
		 * @param[out] plan 接收由实际渲染图编译器生成的帧计划。
		 *
		 * @return 资源导入、上传、清屏、呈现导出及渲染图编译均成功时为 true。
		 */
		bool BuildFramePlan(FakeDevice& device, RenderGraphPlan& plan) {
			const auto view = device.AddTexture({.extent = {32, 24},
			    .format = PixelFormat::RGBA8_SRGB,
			    .usage = RHITextureUsage::ColorAttachment,
			    .presentable = true});
			RHIBufferStateSnapshot source;
			source.desc = {16, RHIBufferUsage::TransferSource, RHIMemoryDomain::Upload};
			source.state.initialized = {{0, 16}};
			const auto staging = device.AddBuffer(std::move(source));
			RHIBufferStateSnapshot destination;
			destination.desc = {16, RHIBufferUsage::TransferDestination, RHIMemoryDomain::Device};
			const auto uploaded = device.AddBuffer(std::move(destination));
			RenderGraph graph(device);
			RGTexture color;
			RGBuffer sourceBuffer, destinationBuffer;
			return graph.ImportBuffer(staging, sourceBuffer).IsSuccess() &&
			    graph.ImportBuffer(uploaded, destinationBuffer).IsSuccess() &&
			    graph.AddCopyPass("Pending Upload", sourceBuffer, destinationBuffer, {.size = 16}).IsSuccess() &&
			    graph.ImportTexture(view, color).IsSuccess() &&
			    graph.AddClearPass("Overwritten Clear", color, {1, 0, 0, 1}).IsSuccess() &&
			    graph.AddClearPass("Test Clear", color, {0, 0, 0, 1}).IsSuccess() &&
			    graph.ExportPresent(color).IsSuccess() && graph.Compile(plan).IsSuccess();
		}

		RHISubmission MakeSubmission(FakeDevice& device, FakeCommandList& commands, uint64_t generation = 1) {
			const RHIFrameToken frame{device.GetDeviceId(), 0, generation};
			return {frame, &commands, {frame, RHIGpuDependencyKind::AcquireReady},
			    {frame, RHIGpuDependencyKind::RenderFinished}};
		}
	}

	void RunFrameLifetimeTests(TestContext& test) {
		FakeDevice device;
		RenderGraphPlan plan;
		if (!BuildFramePlan(device, plan)) {
			test.Check(false, "Clear/present fixture compiles through the real graph");
			return;
		}
		test.Check(
		    plan.GetSchedule().culledPassCount == 1 && plan.GetSchedule().passOrder == std::vector<uint32_t>{0, 2},
		    "Real builder access declarations keep uploads and cull an overwritten clear");
		FakeCommandList commands;
		FrameSubmissionResult result;
		const auto submission = MakeSubmission(device, commands);
		test.Check(FrameScheduler::Execute(plan, device.queue, submission, result).IsSuccess(),
		    "Compiled frame records and submits through the single scheduler");
		test.Check(result.accepted && result.completion.sequence == 1 && result.completions.GetPoints().size() == 1,
		    "Successful submission publishes one immutable whole-batch completion point");
		test.Check(commands.events.size() >= 3 && commands.events.front() == "Begin" &&
		        commands.events[commands.events.size() - 2] == "End" && commands.events.back() == "Submit",
		    "Recording finishes before the only queue submission");
		test.Check(device.queue.lastSubmission.wait == submission.wait &&
		        device.queue.lastSubmission.signal == submission.signal,
		    "Scheduler preserves the exact acquire and render-finished dependencies");
		test.Check(commands.textureUpdates.size() == 1 && !commands.textureUpdates.front().after.initialized &&
		        commands.textureUpdates.front().after.access.layout == RHITextureLayout::Present,
		    "Present export records its layout without promising preserved contents on a later acquire");
		test.Check(commands.copyCount == 1 && commands.bufferUpdates.size() == 2,
		    "Pending uploads remain real recorded side effects even when this frame never draws their destination");
		bool complete = true;
		test.Check(device.queue.IsComplete(result.completion, complete).IsSuccess() && !complete,
		    "Accepted work remains incomplete until separate completion evidence");
		device.queue.completed = 1;
		test.Check(device.queue.IsComplete(result.completion, complete).IsSuccess() && complete,
		    "Completion is tied to the submitted sequence");
		const auto firstPoint = result.completion;

		/* 用同一槽的新generation提交，旧完成身份仍指旧提交，不能随帧槽复用改变。 */
		FakeCommandList next;
		test.Check(FrameScheduler::Execute(plan, device.queue, MakeSubmission(device, next, 2), result).IsSuccess() &&
		        result.completion.sequence == 2,
		    "Reusing a slot creates a new submission identity");
		test.Check(device.queue.IsComplete(firstPoint, complete).IsSuccess() && complete &&
		        device.queue.IsComplete(result.completion, complete).IsSuccess() && !complete,
		    "Old completion remains complete while the reused slot has newer pending work");

		for (const char* failure : {"Begin", "Plan", "BufferBarrier", "CopyBuffer", "TextureBarrier", "BeginRendering",
		         "EndRendering", "End"}) {
			FakeDevice failedDevice;
			FakeCommandList failedCommands;
			failedCommands.failAt = failure;
			test.Check(
			    !FrameScheduler::Execute(plan, failedDevice.queue, MakeSubmission(failedDevice, failedCommands), result)
			            .IsSuccess() &&
			        !result.accepted && !result.completion.HasValue() && result.completions.GetPoints().empty() &&
			        failedDevice.queue.submitCalls == 0,
			    "Any recording failure stops before queue submission and clears previous receipt");
		}
		FakeDevice rejected;
		rejected.queue.failure = {.code = RHIErrorCode::DeviceLost, .operation = "InjectedSubmitFailure"};
		FakeCommandList rejectedCommands;
		test.Check(
		    FrameScheduler::Execute(plan, rejected.queue, MakeSubmission(rejected, rejectedCommands), result).code ==
		            RHIErrorCode::DeviceLost &&
		        !result.accepted && rejected.queue.submitted == 0 && result.completions.GetPoints().empty(),
		    "Rejected submit propagates failure without inventing a completion point");

		FakeDevice invalid;
		FakeCommandList invalidCommands;
		auto wrong = MakeSubmission(invalid, invalidCommands);
		++wrong.wait.source.generation;
		test.Check(!FrameScheduler::Execute(plan, invalid.queue, wrong, result).IsSuccess() &&
		        invalidCommands.events.empty() && invalid.queue.submitCalls == 0,
		    "A stale acquire generation is rejected before recording");
		wrong = MakeSubmission(invalid, invalidCommands);
		wrong.commands = nullptr;
		test.Check(
		    !FrameScheduler::Execute(plan, invalid.queue, wrong, result).IsSuccess(), "Null command list is rejected");
		RenderGraphPlan empty;
		test.Check(!FrameScheduler::Execute(empty, invalid.queue, MakeSubmission(invalid, invalidCommands), result)
		               .IsSuccess() &&
		        invalid.queue.submitCalls == 0,
		    "An invalid plan cannot reach the queue");

		FakeDevice brokenBackend;
		FakeCommandList brokenCommands;
		brokenBackend.queue.returnInvalidCompletion = true;
		test.Check(
		    !FrameScheduler::Execute(plan, brokenBackend.queue, MakeSubmission(brokenBackend, brokenCommands), result)
		            .IsSuccess() &&
		        result.accepted && brokenBackend.queue.submitted == 1,
		    "A backend completion contract violation never erases already accepted work");
		device.queue.failure = {.code = RHIErrorCode::DeviceLost, .operation = "InjectedQueryFailure"};
		complete = true;
		test.Check(device.queue.IsComplete(firstPoint, complete).code == RHIErrorCode::DeviceLost && !complete,
		    "Device loss is not reported as successful retirement");
	}
}
