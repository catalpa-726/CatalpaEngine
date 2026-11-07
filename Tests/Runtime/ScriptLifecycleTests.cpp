#include "catpch.h"
#include "TestRunner.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"

namespace Catalpa::Tests {
    void RunScriptLifecycleTests(TestContext& test) {
        std::string error;
        Scene scene("Script lifecycle contracts");
        const auto actor = PersistentEntityId::FromWords(93, 1);
        if (!scene.QueueCreate(actor, "Actor", {}).IsSuccess() || !scene.CommitPending().IsSuccess()) {
            test.Check(false, "Create script lifecycle fixture");
            return;
        }
        const auto reference = scene.MakeReference(scene.Resolve(actor));
        ScriptApi api{scene, reference, {}};
        ScriptAsset source;
        source.source = R"lua(
            local M={state_schema=1,state={value=3}}
            function M.start() events.subscribe("pulse","on_pulse") tasks.after(0.2,"on_timer") end
            function M.on_pulse(dt,event) scene.translate(tonumber(event.value),0,0) M.state.value=M.state.value+1 events.unsubscribe("pulse") end
            function M.on_timer() scene.translate(0,1,0) end
            return M
        )lua";
        LuaScriptBackend vm;
        test.Check(vm.Start(source, {}, api, error) && api.commands.Commit(scene, error),
                   "Lua starts real bounded event subscriptions and timer continuations");
        ScriptValues event;
        event["value"] = std::string("2");
        test.Check(vm.Event("pulse", event, api, error) && api.commands.Commit(scene, error) &&
                       vm.Event("pulse", event, api, error) && api.commands.Commit(scene, error),
                   "Event unsubscribe prevents a later delivery in the same VM");
        TransformComponent pose;
        scene.GetTransform(scene.Resolve(actor), pose);
        test.Check(pose.local.translation.x == 2,
                   "A subscribed event publishes exactly one protected world write");
        test.Check(vm.Call("update", .1, api, error) && api.commands.Commit(scene, error) &&
                       vm.Call("update", .1, api, error) && api.commands.Commit(scene, error),
                   "Timer resumes after accepted simulation seconds");
        scene.GetTransform(scene.Resolve(actor), pose);
        test.Check(pose.local.translation.y == 1, "Timer callback is a real scene-command consumer");
        ScriptAsset missingCallback;
        missingCallback.source = "return {start=function() events.subscribe('ghost','absent_callback') end}";
        LuaScriptBackend missing;
        ScriptApi missingApi{scene, reference, {}};
        test.Check(
            missing.Start(missingCallback, {}, missingApi, error) &&
                !missing.Event("ghost", {}, missingApi, error),
            "A subscribed callback typo reports an actionable error instead of silently consuming the event");
        uint32_t schema = 0;
        ScriptValues state;
        test.Check(vm.ExportState(schema, state, error) && schema == 1 &&
                       std::get<int64_t>(state.at("value")) == 4,
                   "State snapshot exports exact typed values and schema");
        ScriptAsset next;
        next.source = "return {state_schema=2,state={count=0}, migrate=function(old,s) assert(old==1) return "
                      "{count=s.value} end, start=function() error('reload must not replay start') end}";
        LuaScriptBackend replacement;
        ScriptApi candidate{scene, reference, {}};
        candidate.reloading = true;
        test.Check(replacement.Start(next, {}, candidate, error) &&
                       replacement.ImportState(schema, state, candidate, error),
                   "A new Lua VM migrates explicit state without replaying initial start");
        ScriptValues migrated;
        uint32_t migratedSchema = 0;
        test.Check(replacement.ExportState(migratedSchema, migrated, error) && migratedSchema == 2 &&
                       std::get<int64_t>(migrated.at("count")) == 4,
                   "Migration publishes the destination schema and values");
        ScriptAsset broken;
        broken.source = "return {state_schema=3,state={},migrate=function() error('migration rejected') end}";
        LuaScriptBackend rejected;
        ScriptApi rejectedApi{scene, reference, {}};
        rejectedApi.reloading = true;
        test.Check(rejected.Start(broken, {}, rejectedApi, error) &&
                       !rejected.ImportState(migratedSchema, migrated, rejectedApi, error),
                   "Migration failure rejects only the candidate VM");
        ScriptValues retained;
        uint32_t retainedSchema = 0;
        test.Check(replacement.ExportState(retainedSchema, retained, error) && retained == migrated,
                   "A failed candidate leaves the previous VM state intact");

        ScriptAsset spawner;
        spawner.source = R"lua(
            local M={state_schema=1,state={child=""}}
            function M.start() M.state.child=scene.create("Spawned child",1,2,3) end
            function M.stop() scene.destroy(M.state.child) end
            return M
        )lua";
        LuaScriptBackend spawning;
        ScriptApi spawnApi{scene, reference, {}};
        const auto before = scene.GetWorld().GetEntityCount();
        test.Check(
            spawning.Start(spawner, {}, spawnApi, error) && scene.GetWorld().GetEntityCount() == before &&
                spawnApi.commands.Commit(scene, error) && scene.GetWorld().GetEntityCount() == before + 1,
            "Dynamic entity creation is staged until the structural boundary");
        test.Check(spawning.Call("stop", 0, spawnApi, error) && spawnApi.commands.Commit(scene, error) &&
                       scene.GetWorld().GetEntityCount() == before,
                   "Dynamic subtree deletion uses the Scene lifecycle");
        ScriptAsset badSpawn;
        badSpawn.source = "return {start=function() scene.create('Must not survive') error('rollback') end}";
        LuaScriptBackend failedSpawn;
        ScriptApi failedApi{scene, reference, {}};
        test.Check(!failedSpawn.Start(badSpawn, {}, failedApi, error) &&
                       failedApi.commands.Commit(scene, error) && scene.GetWorld().GetEntityCount() == before,
                   "A Lua exception discards staged structural creation");

        JobSystem jobs;
        if (!jobs.Start({1, 16}, error)) {
            test.Check(false, error);
            return;
        }
        AssetManager assets(jobs);
        if (!RegisterScriptAssetLoader(assets, error)) {
            test.Check(false, error);
            return;
        }
        const auto id = AssetId::FromWords(93, 2);
        ScriptAsset asynchronous;
        asynchronous.source =
            "return {state_schema=1,state={},start=function() tasks.checksum('abc','done') "
            "end,done=function(dt,e) "
            "assert(e.checksum=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad') "
            "scene.translate(0,0,1) end}";
        test.Check(assets.PublishBuiltin<ScriptAsset>(
                       id, 1, std::make_shared<const ScriptAsset>(asynchronous), 4096, error) &&
                       scene.QueueSetScript(scene.Resolve(actor), {id, true, {}}).IsSuccess() &&
                       scene.CommitPending().IsSuccess(),
                   "Publish asynchronous script and attach its real component");
        ScriptRuntime runtime;
        runtime.BindJobs(jobs);
        runtime.Tick(scene, assets, .01);
        bool resumed = false;
        for (int tick = 0; tick < 1000 && !resumed; ++tick) {
            jobs.CollectCompleted();
            runtime.Tick(scene, assets, .01);
            scene.GetTransform(scene.Resolve(actor), pose);
            resumed = pose.local.translation.z == 1;
            if (!resumed)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        test.Check(resumed && runtime.Diagnostics().empty(),
                   "Real Jobs checksum resumes on the owner and publishes an entity command");
        const float retainedX = pose.local.translation.x;
        ScriptAsset overBudget;
        overBudget.source = "return {state_schema=1,state={},update=function() scene.translate(4,0,0) "
                            "events.emit('too_large',string.rep('x',65536)) end}";
        test.Check(assets.PublishBuiltin<ScriptAsset>(id, 2, std::make_shared<const ScriptAsset>(overBudget),
                                                      4096, error),
                   "Publish an over-budget event candidate");
        runtime.Tick(scene, assets, .01);
        scene.GetTransform(scene.Resolve(actor), pose);
        test.Check(!runtime.Diagnostics().empty() && pose.local.translation.x == retainedX,
                   "Event admission failure rejects the same-call world command before publication");
        jobs.CollectCompleted();
        std::vector<JobHandle> occupied;
        for (size_t index = 0; index < 16; ++index) {
            JobHandle job;
            if (!jobs.Submit(
                    [](const CancellationToken& token) {
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                        while (!token.IsCancellationRequested() &&
                               std::chrono::steady_clock::now() < deadline)
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    },
                    job, error))
                break;
            occupied.push_back(job);
        }
        ScriptAsset rejectedAdmission;
        rejectedAdmission.source = "return {state_schema=1,state={},reload=function() scene.translate(8,0,0) "
                                   "tasks.checksum('blocked','done') end,done=function() end}";
        test.Check(occupied.size() == 16 &&
                       assets.PublishBuiltin<ScriptAsset>(
                           id, 3, std::make_shared<const ScriptAsset>(rejectedAdmission), 4096, error),
                   "Fill the shared Jobs admission queue before a hot-reload candidate");
        runtime.Tick(scene, assets, .01);
        scene.GetTransform(scene.Resolve(actor), pose);
        test.Check(!runtime.Diagnostics().empty() && pose.local.translation.x == retainedX,
                   "Rejected async admission leaves the prior VM and same-call world transform unchanged");
        for (const auto& job : occupied)
            job.Cancel();
        for (const auto& job : occupied)
            jobs.Wait(job, error);
        runtime.Stop(&scene);
        jobs.Shutdown();
        test.Check(!runtime.EmitEvent(reference, "late", {}, error),
                   "Stopped runtime rejects stale domain event delivery");
    }
} // namespace Catalpa::Tests
