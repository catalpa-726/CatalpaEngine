#include "TestRunner.h"

#include "Core/Time/SimulationClock.h"
#include "ECS/SystemContext.h"
#include "ECS/SystemScheduler.h"
#include "Runtime/RuntimeServices.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Catalpa::Tests {
	namespace {

		SystemDesc MakeSystem(std::string id, SystemPhase phase, std::vector<SystemAccess> accesses,
		    std::vector<std::string> after, std::function<WorldStatus(SystemContext&)> execute) {
			return {std::move(id), phase, std::move(accesses), std::move(after), std::move(execute)};
		}

		bool Near(double first, double second) {
			return std::abs(first - second) <= 1e-12;
		}

	}

	void RunSystemSchedulerTests(TestContext& context) {
		Scene scene("Scheduler contract");
		FrameTime time;
		RuntimeServices services;
		SystemContext systemContext{scene, time, services};
		const auto success = [](SystemContext&) { return WorldStatus{}; };

		SystemScheduler readOnly;
		std::vector<std::string> order;
		context.Check(
		    readOnly.Add(MakeSystem("second", SystemPhase::Gameplay, {{"Transform", SystemAccessMode::Read}}, {},
		                     [&](SystemContext&) {
			                     order.push_back("second");
			                     return WorldStatus{};
		                     }))
		            .IsSuccess() &&
		        readOnly
		            .Add(MakeSystem("first", SystemPhase::Gameplay, {{"Transform", SystemAccessMode::Read}}, {},
		                [&](SystemContext&) {
			                order.push_back("first");
			                return WorldStatus{};
		                }))
		            .IsSuccess(),
		    "Register independent readers");
		context.Check(readOnly.Compile().IsSuccess() &&
		        readOnly.RunPhase(SystemPhase::Gameplay, systemContext).IsSuccess() &&
		        order == std::vector<std::string>{"second", "first"},
		    "Read/read sharing keeps stable registration order");
		context.Check(
		    readOnly.Add(MakeSystem("first", SystemPhase::Gameplay, {}, {}, success)).code == WorldError::AlreadyExists,
		    "Duplicate system identity is rejected");
		context.Check(
		    readOnly
		            .Add(MakeSystem("bad.access", SystemPhase::Gameplay,
		                {{"Transform", SystemAccessMode::Read}, {"Transform", SystemAccessMode::Write}}, {}, success))
		            .code == WorldError::InvalidArgument,
		    "Duplicate resource declaration cannot hide a write");

		/* 为访问同一资源的系统构造冲突，核对显式顺序对调度的约束。 */
		SystemScheduler conflict;
		context.Check(
		    conflict.Add(MakeSystem(
		                     "writer", SystemPhase::Gameplay, {{"Transform", SystemAccessMode::Write}}, {}, success))
		            .IsSuccess() &&
		        conflict
		            .Add(MakeSystem(
		                "reader", SystemPhase::Gameplay, {{"Transform", SystemAccessMode::Read}}, {}, success))
		            .IsSuccess(),
		    "Register candidate reader and writer");
		context.Check(conflict.Compile().code == WorldError::InvalidArgument &&
		        conflict.GetLastDiagnostic().find("writer") != std::string_view::npos &&
		        conflict.GetLastDiagnostic().find("reader") != std::string_view::npos,
		    "Unordered resource conflict reports both systems");
		context.Check(conflict.RunPhase(SystemPhase::Gameplay, systemContext).code == WorldError::InvalidArgument,
		    "Invalid dependency plan cannot execute");
		context.Check(conflict.Remove("reader").IsSuccess() &&
		        conflict
		            .Add(MakeSystem(
		                "reader", SystemPhase::Gameplay, {{"Transform", SystemAccessMode::Read}}, {"writer"}, success))
		            .IsSuccess() &&
		        conflict.Compile().IsSuccess(),
		    "Explicit dependency resolves the resource conflict");
		context.Check(conflict.Remove("writer").code == WorldError::Busy && conflict.HasSystem("writer"),
		    "Cannot unload a system while another registration explicitly depends on it");
		context.Check(conflict.Remove("reader").IsSuccess() && conflict.Remove("writer").IsSuccess(),
		    "Remove dependent systems before their producer");

		/* 分别构造依赖环、缺失节点和阶段倒序，检查编译拒绝无效系统图。 */
		SystemScheduler cycle;
		context.Check(cycle.Add(MakeSystem("a", SystemPhase::Gameplay, {}, {"b"}, success)).IsSuccess() &&
		        cycle.Add(MakeSystem("b", SystemPhase::Gameplay, {}, {"a"}, success)).IsSuccess() &&
		        cycle.Compile().code == WorldError::InvalidArgument,
		    "Dependency cycle is rejected");
		context.Check(cycle.Clear().IsSuccess() && !cycle.HasSystem("a") && !cycle.HasSystem("b") &&
		        cycle.GetLastDiagnostic().empty(),
		    "Clear discards an invalid dependency cycle and its diagnostic");
		context.Check(cycle.Add(MakeSystem("recovered", SystemPhase::Gameplay, {}, {}, success)).IsSuccess() &&
		        cycle.Compile().IsSuccess() && cycle.RunPhase(SystemPhase::Gameplay, systemContext).IsSuccess(),
		    "A cleared scheduler accepts and executes a new valid plan");
		SystemScheduler unknown;
		context.Check(unknown.Add(MakeSystem("a", SystemPhase::Gameplay, {}, {"missing"}, success)).IsSuccess() &&
		        unknown.Compile().code == WorldError::InvalidArgument,
		    "Missing dependency is diagnosed before execution");
		SystemScheduler reverse;
		context.Check(reverse.Add(MakeSystem("late", SystemPhase::Extract, {}, {}, success)).IsSuccess() &&
		        reverse.Add(MakeSystem("early", SystemPhase::Gameplay, {}, {"late"}, success)).IsSuccess() &&
		        reverse.Compile().code == WorldError::InvalidArgument,
		    "Explicit dependency cannot reverse the phase contract");
		SystemScheduler phases;
		std::vector<SystemPhase> phaseOrder;
		for (const auto phase :
		    {SystemPhase::Extract, SystemPhase::Camera, SystemPhase::Transform, SystemPhase::Gameplay}) {
			const auto id = std::to_string(static_cast<int>(phase));
			context.Check(phases
			                  .Add(MakeSystem(id, phase, {{"Transform", SystemAccessMode::Write}}, {},
			                      [&, phase](SystemContext&) {
				                      phaseOrder.push_back(phase);
				                      return WorldStatus{};
			                      }))
			                  .IsSuccess(),
			    "Register phase adapter");
		}
		context.Check(phases.Compile().IsSuccess(), "Different ordered phases may share written resources");
		for (const auto phase :
		    {SystemPhase::Gameplay, SystemPhase::Transform, SystemPhase::Camera, SystemPhase::Extract})
			context.Check(phases.RunPhase(phase, systemContext).IsSuccess(), "Run the requested phase");
		context.Check(phaseOrder ==
		        std::vector<SystemPhase>{
		            SystemPhase::Gameplay, SystemPhase::Transform, SystemPhase::Camera, SystemPhase::Extract},
		    "RunPhase never executes a system from another phase");

		/* 在系统回调内尝试改变注册表和重入执行，验证运行期间的保护。 */
		SystemScheduler guarded;
		WorldStatus nested, added, removed, cleared;
		context.Check(
		    guarded.Add(MakeSystem("guard", SystemPhase::Gameplay, {}, {},
		                    [&](SystemContext& current) {
			                    nested = guarded.RunPhase(SystemPhase::Gameplay, current);
			                    added = guarded.Add(MakeSystem("during.run", SystemPhase::Gameplay, {}, {}, success));
			                    removed = guarded.Remove("guard");
			                    cleared = guarded.Clear();
			                    return WorldStatus{};
		                    }))
		            .IsSuccess() &&
		        guarded.RunPhase(SystemPhase::Gameplay, systemContext).IsSuccess(),
		    "Execute guard checks inside a system");
		context.Check(nested.code == WorldError::Busy && added.code == WorldError::Busy &&
		        removed.code == WorldError::Busy && cleared.code == WorldError::Busy &&
		        !guarded.HasSystem("during.run") && guarded.HasSystem("guard") && !guarded.IsRunning(),
		    "Execution rejects reentry, registration mutation and Clear, then releases its guard");
		SystemScheduler failure;
		bool ranAfterFailure = false;
		context.Check(
		    failure.Add(MakeSystem("throw", SystemPhase::Gameplay, {}, {},
		                    [](SystemContext&) -> WorldStatus {
			                    throw std::runtime_error("Deliberate scheduler contract failure");
		                    }))
		            .IsSuccess() &&
		        failure
		            .Add(MakeSystem("after.throw", SystemPhase::Gameplay, {}, {"throw"},
		                [&](SystemContext&) {
			                ranAfterFailure = true;
			                return WorldStatus{};
		                }))
		            .IsSuccess(),
		    "Register deliberate callback failure");
		context.Check(failure.RunPhase(SystemPhase::Gameplay, systemContext).code == WorldError::CommandFailed &&
		        !ranAfterFailure && !failure.IsRunning(),
		    "Callback failure stops the phase and clears execution state");

		SceneManager manager;
		SceneHandle managedHandle, alternateHandle;
		context.Check(manager.CreateScene("Managed scheduler", managedHandle).IsSuccess() &&
		        manager.CreateScene("Alternate scheduler scene", alternateHandle).IsSuccess() &&
		        manager.SetActive(managedHandle).IsSuccess(),
		    "Create and activate scenes for lifecycle guards");
		WorldStatus activeUnload, activeSwitch, activeClear;
		bool enteredActiveScope = false;
		context.Check(
		    manager.WithActive([&](Scene&) {
			           enteredActiveScope = true;
			           activeUnload = manager.Unload(managedHandle);
			           activeSwitch = manager.SetActive(alternateHandle);
			           activeClear = manager.Clear();
			           return WorldStatus{};
		           })
		            .IsSuccess() &&
		        enteredActiveScope && activeUnload.code == WorldError::Busy && activeSwitch.code == WorldError::Busy &&
		        activeClear.code == WorldError::Busy && manager.GetScene(managedHandle) != nullptr,
		    "WithActive prevents unload, activation changes and clearing its borrowed scene");
		context.Check(manager.SetActive(alternateHandle).IsSuccess() && manager.SetActive(managedHandle).IsSuccess(),
		    "Scene activation changes succeed after the active operation exits");
		if (Scene* managed = manager.GetScene(managedHandle)) {
			WorldStatus directUnload, directSwitch, directClear;
			context.Check(managed->GetSystems()
			                  .Add(MakeSystem("lifecycle", SystemPhase::Gameplay, {}, {},
			                      [&](SystemContext&) {
				                      directUnload = manager.Unload(managedHandle);
				                      directSwitch = manager.SetActive(alternateHandle);
				                      directClear = manager.Clear();
				                      return WorldStatus{};
			                      }))
			                  .IsSuccess(),
			    "Register a directly invoked scene system");
			SystemContext directContext{*managed, time, services};
			context.Check(managed->GetSystems().RunPhase(SystemPhase::Gameplay, directContext).IsSuccess() &&
			        directUnload.code == WorldError::Busy && directSwitch.code == WorldError::Busy &&
			        directClear.code == WorldError::Busy && manager.GetScene(managedHandle) == managed,
			    "Manager protects a running scene scheduler even outside WithActive");
		} else
			context.Check(false, "Managed scene remains available for direct scheduler guard checks");
		context.Check(manager.Unload(managedHandle).IsSuccess() && manager.SetActive(alternateHandle).IsSuccess() &&
		        manager.Clear().IsSuccess() && manager.GetScene(alternateHandle) == nullptr,
		    "Unload, activation and Clear succeed once all scene operations and systems finish");

		/* 使用可精确表示的固定步长检查追帧上限、暂停和单步。 */
		SimulationClock clock;
		const SimulationClockConfig binaryStep{0.125, 1.0, 1.0, 2};
		context.Check(clock.Configure(binaryStep).IsSuccess(), "Configure fixed simulation seconds");
		FrameTime frame;
		context.Check(clock.Advance(0.0625, frame).IsSuccess() && frame.tickCount == 0 && frame.firstTick == 1 &&
		        Near(frame.interpolationAlpha, 0.5),
		    "Fractional frame accumulates without advancing a tick");
		context.Check(clock.Advance(0.0625, frame).IsSuccess() && frame.tickCount == 1 && frame.firstTick == 1 &&
		        Near(frame.simulationSeconds, 0.125) && Near(frame.interpolationAlpha, 0.0),
		    "Accumulated seconds form exactly one fixed tick");
		context.Check(clock.Advance(0.5, frame).IsSuccess() && frame.tickCount == 2 && frame.firstTick == 2 &&
		        Near(frame.simulationSeconds, 0.375) && Near(frame.droppedSeconds, 0.25),
		    "Catch-up limit executes bounded ticks and reports discarded simulation seconds");

		clock.SetPaused(true);
		context.Check(clock.Advance(100.0, frame).IsSuccess() && frame.tickCount == 0 &&
		        Near(frame.simulationSeconds, 0.375) && Near(frame.interpolationAlpha, 1.0),
		    "Paused wall time does not accumulate and displays the current authoritative state");
		context.Check(clock.RequestSingleStep().IsSuccess() && !clock.RequestSingleStep().IsSuccess(),
		    "Exactly one paused step may be pending");
		FrameTime sentinel;
		sentinel.realDeltaSeconds = 99.0;
		sentinel.firstTick = 777;
		context.Check(!clock.Advance(std::numeric_limits<double>::quiet_NaN(), sentinel).IsSuccess() &&
		        sentinel.realDeltaSeconds == 99.0 && sentinel.firstTick == 777,
		    "Invalid frame duration preserves the caller output");
		context.Check(clock.Advance(0.0, frame).IsSuccess() && frame.tickCount == 1 && frame.firstTick == 4 &&
		        Near(frame.simulationSeconds, 0.5),
		    "Invalid input did not consume the pending paused step");
		context.Check(clock.Advance(0.0, frame).IsSuccess() && frame.tickCount == 0,
		    "Single-step request is consumed exactly once");
		clock.SetPaused(false);
		context.Check(!clock.RequestSingleStep().IsSuccess() && clock.Advance(0.0, frame).IsSuccess() &&
		        frame.tickCount == 0 && frame.firstTick == 5,
		    "Resume does not replay paused wall time");
		SimulationClockConfig invalid = binaryStep;
		invalid.maxTicksPerFrame = 0;
		context.Check(!clock.Configure(invalid).IsSuccess() && clock.GetConfig().maxTicksPerFrame == 2,
		    "Invalid configuration preserves the active clock configuration");
		context.Check(!clock.Advance(-1.0, sentinel).IsSuccess() && sentinel.firstTick == 777,
		    "Negative frame duration is rejected without publication");

		SimulationClock scaled;
		context.Check(scaled.Configure({0.125, 2.0, 0.25, 2}).IsSuccess() && scaled.Advance(0.5, frame).IsSuccess() &&
		        frame.tickCount == 2 && Near(frame.simulationSeconds, 0.25) && Near(frame.droppedSeconds, 0.75),
		    "Frame cap and catch-up cap report scaled simulation seconds without changing the step unit");
		SimulationClock fine, coarse;
		const SimulationClockConfig partition{0.125, 1.0, 1.0, 8};
		context.Check(fine.Configure(partition).IsSuccess() && coarse.Configure(partition).IsSuccess(),
		    "Configure two clocks for frame partition comparison");
		FrameTime fineFrame, coarseFrame;
		for (int i = 0; i < 8; ++i)
			context.Check(fine.Advance(0.0625, fineFrame).IsSuccess(), "Advance a fine display frame");
		for (int i = 0; i < 4; ++i)
			context.Check(coarse.Advance(0.125, coarseFrame).IsSuccess(), "Advance a coarse display frame");
		context.Check(Near(fineFrame.simulationSeconds, 0.5) && Near(coarseFrame.simulationSeconds, 0.5) &&
		        Near(fineFrame.droppedSeconds, 0.0) && Near(coarseFrame.droppedSeconds, 0.0),
		    "Equivalent accepted time partitions produce the same fixed simulation duration");
		SimulationClock range;
		context.Check(range.Configure({1e100, 1.0, 1e100, 1}).IsSuccess() && range.Advance(1e100, frame).IsSuccess() &&
		        range.Configure(binaryStep).IsSuccess(),
		    "Prepare a clock whose accumulated time dwarfs one fixed step");
		context.Check(range.Advance(0.125, sentinel).code == ClockError::Overflow && sentinel.firstTick == 777,
		    "A step that cannot advance represented total seconds is rejected without publishing a tick");
	}

}
