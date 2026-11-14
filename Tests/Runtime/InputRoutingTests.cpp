#include "TestRunner.h"
#include "Runtime/Input/InputActions.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

#include <cmath>

namespace Catalpa::Tests {
	namespace {
		ActionValue Value(TestContext& context, const ActionSnapshot& snapshot, std::string_view id) {
			const auto* action = snapshot.Find(id);
			context.Check(action != nullptr, "Configured action is present in snapshot");
			return action ? *action : ActionValue{};
		}
	}

	void RunInputRoutingTests(TestContext& context) {
		InputActions input;
		context.Check(input.Configure(MakeSandboxActionMap()).IsSuccess(), "Sandbox input profile configures");
		input.OnEvent(KeyPressedEvent{Key::W});
		auto frame = input.BeginFrame();
		context.Check(Value(context, frame, "move.forward").pressed, "W creates a frame press edge");
		(void)input.BeginFrame(); /* 零 tick 帧不能吃掉待消费边沿。 */
		auto tick = input.ConsumeTick();
		auto forward = Value(context, tick, "move.forward");
		context.Check(
		    forward.held && forward.pressed && forward.value == 1.0, "Pending press survives a zero-tick frame");
		tick = input.ConsumeTick();
		forward = Value(context, tick, "move.forward");
		context.Check(forward.held && !forward.pressed && forward.value == 1.0,
		    "Second catch-up tick holds level without duplicate press");
		input.OnEvent(KeyPressedEvent{Key::W, true});
		frame = input.BeginFrame();
		context.Check(!Value(context, frame, "move.forward").pressed, "Keyboard repeat does not create another press");
		input.OnEvent(KeyReleasedEvent{Key::W});
		input.OnEvent(KeyPressedEvent{Key::W});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "move.forward").held && Value(context, frame, "move.forward").pressed &&
		        Value(context, frame, "move.forward").released,
		    "Digital axis retains release/repress within one event pump");

		frame = input.BeginFrame({.keyboardCaptured = true});
		forward = Value(context, frame, "move.forward");
		context.Check(
		    !forward.held && forward.released && forward.value == 0.0, "UI capture releases held gameplay input");
		tick = input.ConsumeTick();
		context.Check(!Value(context, tick, "move.forward").pressed, "UI capture discards old pending press");
		frame = input.BeginFrame();
		context.Check(
		    !Value(context, frame, "move.forward").held, "Captured key must be released before gameplay resumes");
		input.OnEvent(KeyReleasedEvent{Key::W});
		(void)input.BeginFrame();
		input.OnEvent(KeyPressedEvent{Key::W});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "move.forward").held, "A fresh press after release is accepted");
		(void)input.ConsumeTick();

		input.OnEvent(WindowLostFocusEvent{});
		input.OnEvent(KeyPressedEvent{Key::D});
		frame = input.BeginFrame();
		context.Check(
		    !frame.focused && !Value(context, frame, "move.forward").held && !Value(context, frame, "move.right").held,
		    "Lost focus releases all physical state and ignores new presses");
		input.OnEvent(WindowFocusEvent{});
		input.OnEvent(KeyPressedEvent{Key::W, true});
		frame = input.BeginFrame();
		context.Check(frame.focused && !Value(context, frame, "move.forward").held,
		    "Focus regain and key repeat do not revive a stale held key");

		input.Reset();
		input.OnEvent(KeyPressedEvent{Key::F6});
		input.OnEvent(KeyReleasedEvent{Key::F6});
		(void)input.BeginFrame();
		(void)input.BeginFrame();
		auto pause = Value(context, input.ConsumeTick(), "simulation.pause");
		context.Check(pause.pressed && pause.released && !pause.held,
		    "Fast press/release survives event pump and zero-tick frame");
		pause = Value(context, input.ConsumeTick(), "simulation.pause");
		context.Check(!pause.pressed && !pause.released, "Fast tap edges are consumed once");
		input.OnEvent(KeyPressedEvent{Key::F7});
		frame = input.BeginFrame({.keyboardCaptured = true});
		context.Check(
		    !Value(context, frame, "simulation.step").pressed, "System keys also respect UI keyboard capture");

		input.Reset();
		input.OnEvent(MouseMovedEvent{0.0f, 0.0f});
		input.OnEvent(MouseMovedEvent{80.0f, 40.0f});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "look.yaw").value == 0.0, "Mouse look requires explicit right-button gate");
		input.OnEvent(MouseButtonPressedEvent{Mouse::ButtonRight});
		input.OnEvent(MouseMovedEvent{100.0f, 100.0f});
		input.OnEvent(MouseMovedEvent{110.0f, 95.0f});
		frame = input.BeginFrame();
		context.Check(
		    Value(context, frame, "look.yaw").value == 10.0 && Value(context, frame, "look.pitch").value == -5.0,
		    "Look delta uses logical coordinates and excludes movement before gate press");
		tick = input.ConsumeTick();
		context.Check(Value(context, tick, "look.yaw").value == 10.0, "First tick consumes pointer delta");
		tick = input.ConsumeTick();
		context.Check(Value(context, tick, "look.yaw").value == 0.0, "Catch-up tick cannot multiply mouse motion");
		input.OnEvent(MouseMovedEvent{114.0f, 95.0f});
		(void)input.BeginFrame();
		input.OnEvent(MouseMovedEvent{117.0f, 95.0f});
		(void)input.BeginFrame();
		tick = input.ConsumeTick();
		context.Check(
		    Value(context, tick, "look.yaw").value == 7.0, "Pointer delta accumulates across frames without ticks");
		input.OnEvent(MouseMovedEvent{130.0f, 95.0f});
		(void)input.BeginFrame({.mouseCaptured = true});
		tick = input.ConsumeTick();
		context.Check(Value(context, tick, "look.yaw").value == 0.0, "UI mouse capture cancels pending motion");
		input.OnEvent(MouseMovedEvent{140.0f, 95.0f});
		frame = input.BeginFrame();
		context.Check(
		    Value(context, frame, "look.yaw").value == 0.0, "Captured held mouse gate remains blocked until release");

		input.Reset();
		input.OnEvent(KeyPressedEvent{Key::W});
		input.OnEvent(KeyPressedEvent{Key::F6});
		frame = input.BeginFrame({.gameplayActive = false});
		context.Check(!Value(context, frame, "move.forward").held && Value(context, frame, "simulation.pause").pressed,
		    "Inactive gameplay context suppresses movement but leaves uncaptured system controls");

		const auto previousCount = input.GetFrameSnapshot().actions.size();
		auto invalid = MakeSandboxActionMap();
		invalid.actions.push_back(invalid.actions.front());
		context.Check(!input.Configure(std::move(invalid)).IsSuccess() &&
		        input.GetFrameSnapshot().actions.size() == previousCount,
		    "Duplicate action IDs reject transactionally");
		context.Check(!input.ParseProfile(R"({"schemaVersion":2,"profile":"sandbox","actions":[]})").IsSuccess(),
		    "Unknown profile version rejects");
		context.Check(!input.ParseProfile("not json").IsSuccess(), "Malformed JSON rejects without default fallback");
		context.Check(
		    input
		        .ParseProfile(
		            R"({"schemaVersion":1,"profile":"test","actions":[{"id":"move.forward","context":"gameplay","type":"axis","bindings":[{"source":"key","code":"W","scale":1}]}]})")
		        .IsSuccess(),
		    "A valid JSON profile is parsed and consumed");
		input.OnEvent(KeyPressedEvent{Key::W});
		frame = input.BeginFrame();
		context.Check(frame.actions.size() == 1 && Value(context, frame, "move.forward").held,
		    "Parsed rebinding drives the live action map");
		input.OnEvent(KeyPressedEvent{static_cast<KeyCode>(65535)});
		(void)input.BeginFrame();
		context.Check(input.GetPhysicalSnapshot().keys[Key::W],
		    "Invalid platform key is rejected without disturbing valid state");

		ActionMap buttons{.profile = "buttons",
		    .actions = {
		        {.id = "button", .bindings = {{InputSource::Key, Key::F6, 1.0}, {InputSource::Key, Key::F7, 1.0}}}}};
		context.Check(
		    input.Configure(std::move(buttons)).IsSuccess(), "Multiple physical bindings configure one button");
		input.OnEvent(KeyPressedEvent{Key::F6});
		(void)input.BeginFrame();
		(void)input.ConsumeTick();
		input.OnEvent(KeyPressedEvent{Key::F7});
		input.OnEvent(KeyReleasedEvent{Key::F7});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "button").held && !Value(context, frame, "button").pressed,
		    "Another physical binding cannot retrigger a held action");
		input.OnEvent(KeyReleasedEvent{Key::F6});
		input.OnEvent(KeyPressedEvent{Key::F6});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "button").held && Value(context, frame, "button").pressed &&
		        Value(context, frame, "button").released,
		    "Release and repress within one event pump retain both button edges");

		context.Check(input.Configure(MakeSandboxActionMap()).IsSuccess(), "Context switch fixture configures");
		input.OnEvent(KeyPressedEvent{Key::W});
		input.OnEvent(KeyPressedEvent{Key::F6});
		input.OnEvent(MouseButtonPressedEvent{Mouse::ButtonRight});
		input.OnEvent(MouseMovedEvent{10.0f, 10.0f});
		input.OnEvent(MouseMovedEvent{18.0f, 10.0f});
		(void)input.BeginFrame();
		input.ResetGameplayContext();
		tick = input.ConsumeTick();
		context.Check(!Value(context, tick, "move.forward").held && !Value(context, tick, "move.forward").pressed &&
		        Value(context, tick, "look.yaw").value == 0.0,
		    "World switch cancels gameplay levels, pending edges and motion");
		context.Check(Value(context, tick, "simulation.pause").pressed, "World switch preserves pending system action");
		frame = input.BeginFrame();
		context.Check(!Value(context, frame, "move.forward").held, "Old world held key is blocked until release");
		input.OnEvent(KeyReleasedEvent{Key::W});
		(void)input.BeginFrame();
		input.OnEvent(KeyPressedEvent{Key::W});
		frame = input.BeginFrame();
		context.Check(Value(context, frame, "move.forward").held && Value(context, frame, "move.forward").pressed,
		    "New world receives only a fresh gameplay press");
		input.Reset();
		input.OnEvent(KeyPressedEvent{Key::W});
		input.OnEvent(KeyReleasedEvent{Key::W});
		input.ResetGameplayContext();
		(void)input.BeginFrame();
		tick = input.ConsumeTick();
		context.Check(!Value(context, tick, "move.forward").pressed && !Value(context, tick, "move.forward").released,
		    "Old event-pump quick tap cannot enter the new world before first frame publication");
	}
}
