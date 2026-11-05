#include "TestRunner.h"
#include "Core/Time/SimulationClock.h"
#include "Renderer/RendererFrameContext.h"
#include "Runtime/GameplayModule.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include <cstring>

namespace Catalpa::Tests {
    namespace {
        // 用实际生产 packing 读取时间，不通过测试自制的 GPU 常量模拟暂停行为。
        float PackedSimulationTime(const FrameTime& time, TestContext& test)
        {
            RenderFramePacket packet;
            packet.sequence = 1;
            packet.time = time;
            RenderView view;
            view.id = 1;
            view.camera = { 1, 0, 1 };
            view.historyRevision = 1;
            const bool viewValid = BuildPresentationView(view, { 16, 16 }, view).IsSuccess();
            test.Check(viewValid, "Time fixture uses the production presentation projection");
            if (viewValid) packet.views.push_back(view);
            PackedFrameUniforms packed;
            const bool valid = BuildFrameUniformData(packet, {}, 256, 256, packed).IsSuccess();
            test.Check(valid, "Clock snapshot packs through the production frame ABI");
            FrameGpuParameters frame;
            if (valid && packed.data.size() >= sizeof(frame)) std::memcpy(&frame, packed.data.data(), sizeof(frame));
            return frame.time[0];
        }
    }

    void RunSimulationLifecycleTests(TestContext& test)
    {
        SimulationClock clock;
        const SimulationClockConfig config{ .fixedDeltaSeconds = 0.125, .maxFrameDeltaSeconds = 0.5 };
        test.Check(clock.Configure(config).IsSuccess(), "Lifecycle fixture uses exactly representable fixed seconds");
        FrameTime time;
        test.Check(clock.Advance(0.0625, time).IsSuccess() && time.tickCount == 0
            && time.interpolationAlpha == 0.5, "Fractional frames accumulate without a simulation tick");
        test.Check(clock.Advance(0.0625, time).IsSuccess() && time.tickCount == 1
            && PackedSimulationTime(time, test) == 0.125f, "Accumulated fixed tick feeds material time");

        clock.SetPaused(true);
        test.Check(clock.Advance(0.25, time).IsSuccess() && time.tickCount == 0
            && PackedSimulationTime(time, test) == 0.125f, "Paused wall time does not animate simulation-driven material effects");
        test.Check(clock.RequestSingleStep().IsSuccess() && clock.Advance(0, time).IsSuccess()
            && time.tickCount == 1 && PackedSimulationTime(time, test) == 0.25f,
            "A paused single step advances exactly one material-time quantum");
        clock.SetPaused(false);
        auto scaled = config;
        scaled.timeScale = 2;
        test.Check(clock.Configure(scaled).IsSuccess() && clock.Advance(0.0625, time).IsSuccess()
            && time.tickCount == 1 && PackedSimulationTime(time, test) == 0.375f,
            "Time scale affects accepted simulation time without an editor-local clock");
        scaled.timeScale = 0;
        test.Check(clock.Configure(scaled).IsSuccess() && clock.Advance(0.25, time).IsSuccess()
            && time.tickCount == 0 && PackedSimulationTime(time, test) == 0.375f,
            "Zero time scale keeps material phase stable while presentation continues");
        clock.Reset();
        test.Check(clock.Advance(0, time).IsSuccess() && time.firstTick == 1
            && PackedSimulationTime(time, test) == 0, "Restart resets simulation identity and material phase");

        // 退出时释放真正注册回调的捕获物；不通过等待或创建设备模拟生命周期。
        SceneManager scenes;
        SceneHandle handle;
        test.Check(scenes.CreateScene("Lifecycle", handle).IsSuccess(), "Create isolated lifecycle scene");
        auto* scene = scenes.GetScene(handle);
        if (!scene) return;
        GameplayModule module;
        auto marker = std::make_shared<int>(0);
        std::weak_ptr<int> weak = marker;
        SystemDesc system;
        system.id = "test.lifecycle";
        system.phase = SystemPhase::Gameplay;
        system.execute = [marker](SystemContext&) { ++*marker; return WorldStatus{}; };
        test.Check(module.Install(*scene, std::move(system)).IsSuccess(), "Install a callback owned by the scene scheduler");
        system = {}; // 清除调用方副本，只观测调度器对捕获物的保活。
        marker.reset();
        test.Check(!weak.expired() && module.Uninstall(*scene).IsSuccess() && weak.expired(),
            "Uninstall releases captured work before unloading the scene");
        test.Check(scenes.Unload(handle).IsSuccess() && !module.IsInstalled(),
            "Scene unload follows gameplay callback release");
    }
}
