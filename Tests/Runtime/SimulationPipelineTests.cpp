#include "TestRunner.h"

#include "Runtime/GameplayModule.h"
#include "Runtime/RuntimeServices.h"
#include "ECS/SystemContext.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Renderer/RenderScene.h"

namespace Catalpa::Tests {
    // 验证真实模块注册/场景安全点/提取所有权的连接，使用 CPU 服务而非模拟 GPU。
    void RunSimulationPipelineTests(TestContext& test)
    {
        SceneManager scenes;
        SceneHandle handle;
        test.Check(scenes.CreateScene("Pipeline test", handle).IsSuccess(), "Pipeline creates a scene");
        test.Check(scenes.SetActive(handle).IsSuccess(), "Pipeline activates its scene");
        Scene* scene = scenes.GetScene(handle);
        if (!scene) return;
        const auto persistent = PersistentEntityId::FromWords(0x5330333200000100ULL, 1);
        test.Check(scene->QueueCreate(persistent, "Moving entity", {}).IsSuccess(), "Pipeline queues entity");
        test.Check(scenes.UpdateActive().IsSuccess(), "Pipeline commits initial state");
        FrameTime time;
        time.tickCount = 1;
        RuntimeServices services;
        GameplayModule gameplay;
        unsigned calls = 0;
        SystemDesc system;
        system.id = "test.gameplay";
        system.phase = SystemPhase::Gameplay;
        system.accesses = { { "scene.commands", SystemAccessMode::Write } };
        system.execute = [&](SystemContext& context) {
            ++calls;
            test.Check(scenes.Unload(handle).code == WorldError::Busy, "Running systems cannot unload their scene");
            test.Check(scenes.SetActive(handle).code == WorldError::Busy, "Running systems cannot switch active scene");
            Math::TransformTRS moved;
            moved.translation.x = 3.0f;
            return context.scene.QueueSetLocal(context.scene.Resolve(persistent), moved);
        };
        test.Check(gameplay.Install(*scene, std::move(system)).IsSuccess(), "Gameplay installs a complete system");
        test.Check(gameplay.IsInstalled(), "Gameplay publishes registration only after compilation");
        const auto status = scenes.WithActive([&](Scene& active) {
            SystemContext context{ active, time, services };
            if (const auto executed = active.GetSystems().RunPhase(SystemPhase::Gameplay, context); !executed.IsSuccess()) return executed;
            if (const auto committed = active.CommitPending(); !committed.IsSuccess()) return committed;
            return TransformSystem::Update(active);
        });
        test.Check(status.IsSuccess() && calls == 1, "Gameplay runs exactly once inside protected update");
        RenderFramePacket packet;
        test.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, packet).IsSuccess(), "Pipeline extracts stable values");
        test.Check(packet.transforms.size() == 1 && packet.transforms.front().worldMatrix[3].x == 3.0f, "Committed movement reaches extracted frame");
        RenderScene render;
        test.Check(render.Apply(std::move(packet)).IsSuccess(), "Renderer consumes owned frame");
        test.Check(gameplay.Uninstall(*scene).IsSuccess() && !gameplay.IsInstalled(), "Gameplay uninstalls before scene destruction");
        test.Check(scenes.Unload(handle).IsSuccess(), "Scene unloads after execution scope");
        test.Check(render.GetFrame().transforms.size() == 1 && render.GetFrame().transforms.front().worldMatrix[3].x == 3.0f, "Renderer snapshot survives its source scene");
        RenderFramePacket empty;
        empty.sequence = 2;
        test.Check(render.Apply(std::move(empty)).IsSuccess() && render.GetFrame().transforms.empty(), "New empty frame clears unloaded source");
    }
}
