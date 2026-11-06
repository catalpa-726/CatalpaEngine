#include "catpch.h"
#include "TestRunner.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/ScriptValueArchive.h"
#include "Scripting/BehaviorGraphRuntime.h"
#include "Assets/AssetManager.h"
#include "Documents/SceneDocument.h"
#include "ECS/SystemContext.h"
#include "Runtime/RuntimeServices.h"

namespace Catalpa::Tests {
    namespace {
        const auto Actor=PersistentEntityId::FromWords(0x5343524950545445ULL,1);
        Math::Vec3 Position(Scene& scene) {
            TransformComponent value;
            if(!scene.GetTransform(scene.Resolve(Actor),value).IsSuccess()) throw std::runtime_error("Script fixture actor is unavailable");
            return value.local.translation;
        }
        BehaviorNode Node(uint64_t id,std::string type) {BehaviorNode node;node.id=StableId::FromWords(0x4245484156494f52ULL,id);node.type=std::move(type);return node;}
    }

    void RunScriptingTests(TestContext& test) {
        std::string error;
        SceneManager scenes;SceneHandle handle;
        test.Check(scenes.CreateScene("Script author",handle).IsSuccess(),"Create a real CPU scene for Lua commands");
        auto* scene=scenes.GetScene(handle);if(!scene)return;
        test.Check(scene->QueueCreate(Actor,"Actor",{}).IsSuccess() && scene->CommitPending().IsSuccess(),"Create a script actor");
        ScriptApi api{*scene,scene->MakeReference(scene->Resolve(Actor)),{}};
        ScriptAsset source;
        source.source="return { start=function() scene.translate(0,1,0) end, update=function(dt) scene.translate(0,dt,0) local x,y,z=scene.position() assert(y>=1) end }";
        LuaScriptBackend vm;
        test.Check(vm.Start(source,{},api,error) && Position(*scene)==Math::Vec3(0),"Successful Lua initialization is staged until its command commit");
        test.Check(api.commands.Commit(*scene,error) && vm.Call("update",.25,api,error) && api.commands.Commit(*scene,error) &&
            Position(*scene)==Math::Vec3(0,1.25f,0),"Lua lifecycle uses seconds and publishes checked translation commands");
        ScriptAsset broken;broken.source="return { start=function() scene.translate(0,100,0) error('fixture failure') end }";
        LuaScriptBackend rejected;
        const auto before=Position(*scene);
        test.Check(!rejected.Start(broken,{},api,error) && api.commands.Commit(*scene,error) && Position(*scene)==before,
            "A failed Lua start discards every staged scene write");
        LuaScriptBackend spinning;
        ScriptAsset loop;loop.source="while true do end";
        test.Check(!spinning.Start(loop,{},api,error) && error.find("budget")!=error.npos,"A Lua instruction loop stops at the execution budget");
        LuaScriptBackend numberError;
        loop.source="error(123)";
        test.Check(!numberError.Start(loop,{},api,error),"Non-string Lua errors return a host diagnostic without unprotected conversion");
        test.Check(!LuaScriptBackend::CheckSource("return { update = function( }",error),"Lua syntax errors are detected before Cook publication");
        LuaScriptBackend sandbox;
        loop.source="assert(io==nil and os==nil and package==nil and debug==nil and load==nil); return {}";
        test.Check(sandbox.Start(loop,{},api,error),"Lua source cannot acquire filesystem, process, debug or dynamic loader libraries");
        LuaScriptBackend modules;
        auto helper = std::make_shared<ScriptAsset>();
        helper->source = "return { value=7 }";
        ScriptAsset requiring;
        requiring.source = "local h=require('helper'); assert(h.value==7); return {}";
        test.Check(modules.SetModules({{"helper", helper}}, error) && modules.Start(requiring, {}, api, error),
            "Declared Lua module values are evaluated and cached inside the instance");
        LuaScriptBackend cyclicModules;
        auto cycleModule = std::make_shared<ScriptAsset>();
        cycleModule->source = "return require('helper')";
        test.Check(cyclicModules.SetModules({{"helper", cycleModule}}, error) && !cyclicModules.Start(requiring, {}, api, error) &&
            error.find("cycle") != error.npos, "Recursive Lua require fails without an unbounded module call chain");
        auto memory = std::make_shared<ScriptMemoryBudget>();
        {
            LuaScriptBackend quota(memory);
            ScriptAsset oversized;
            oversized.source = "local value=string.rep('x',9*1024*1024); return {}";
            test.Check(!quota.Start(oversized, {}, api, error) && memory->used <= memory->limit,
                "A Lua allocation beyond the per-instance quota fails inside the protected VM call");
        }
        test.Check(memory->used == 0, "Closing the Lua VM releases its aggregate memory charge");
        auto propertyMemory = std::make_shared<ScriptMemoryBudget>();
        propertyMemory->limit = 64 * 1024;
        {
            LuaScriptBackend propertyQuota(propertyMemory);
            ScriptAsset empty; empty.source = "return {}";
            ScriptValues properties{{"text", std::string(65536, 'x')}};
            test.Check(!propertyQuota.Start(empty, properties, api, error),
                "Property allocation errors unwind through a protected Lua callback without live C++ iterators");
        }
        test.Check(propertyMemory->used == 0, "Failed property initialization releases the whole Lua allocation charge");
        LuaScriptBackend propertyTypes;
        ScriptAsset inspectProperties;
        inspectProperties.source = "assert(properties.target == self and properties.vector[3] == 3); return {}";
        test.Check(propertyTypes.Start(inspectProperties, {{"target", Actor}, {"vector", Math::Vec3(1,2,3)}}, api, error),
            "Prepared Lua properties retain stable entity text and vector coordinates");
        ScriptValues values{{"flag",true},{"count",int64_t(7)},{"vector",Math::Vec3(1,2,3)},{"target",Actor}};
        auto independent = values;
        independent["count"] = int64_t(9);
        auto moved = std::move(independent);
        independent["count"] = int64_t(11);
        test.Check(std::get<int64_t>(values.at("count")) == 7 && std::get<int64_t>(moved.at("count")) == 9 &&
            std::get<int64_t>(independent.at("count")) == 11,
            "Component value maps deep-copy and remain independently writable after a nonallocating move");
        ArchiveValue encoded;ScriptValues restored;
        test.Check(EncodeScriptValues(values,encoded,error) && DecodeScriptValues(encoded,restored,error) && restored==values,
            "Script author values use explicit stable type tags rather than variant indices");
        values["invalid"]=std::numeric_limits<double>::infinity();
        test.Check(!ValidateScriptValues(values,error),"Non-finite script properties are rejected");
        test.Check(!ValidateBehaviorVariables({{"count",int64_t(9007199254740993LL)}}, error),
            "Behavior integer overrides cannot silently lose double precision");

        JobSystem jobs;AssetManager assets(jobs);
        test.Check(RegisterScriptAssetLoader(assets,error) && RegisterBehaviorGraphAssetLoader(assets,error),"Register real script and behavior loaders");
        const auto scriptId=AssetId::FromWords(0x5343524950545445ULL,10);
        ScriptAsset ticking;ticking.source="return { update=function(dt) scene.translate(dt,0,0) end }";
        test.Check(assets.PublishBuiltin<ScriptAsset>(scriptId,1,std::make_shared<const ScriptAsset>(ticking),4096,error),"Publish a versioned script fixture");
        test.Check(scene->QueueSetScript(scene->Resolve(Actor),ScriptComponent{scriptId,true,{}}).IsSuccess() && scene->CommitPending().IsSuccess(),"Attach an author script component");
        ScriptRuntime runtime;runtime.Tick(*scene,assets,.25);
        test.Check(Position(*scene).x==.25f,"ScriptRuntime runs its own instance against the scene owner");
        runtime.Stop(scene);
        auto dependencyId = scriptId;
        bool sharedModules = true;
        for (uint64_t level = 0; level < 24; ++level) {
            ScriptAsset shared = ticking;
            shared.modules.emplace("left_" + std::to_string(level), dependencyId);
            shared.modules.emplace("right_" + std::to_string(level), dependencyId);
            const auto id = AssetId::FromWords(0x5343524950545445ULL, 100 + level);
            sharedModules = assets.PublishBuiltin<ScriptAsset>(id, 1, std::make_shared<const ScriptAsset>(shared), 4096, error)
                && sharedModules;
            dependencyId = id;
        }
        test.Check(sharedModules && scene->QueueSetScript(scene->Resolve(Actor), {dependencyId, true, {}}).IsSuccess() &&
            scene->CommitPending().IsSuccess(), "Attach a bounded module DAG with repeatedly shared children");
        const auto beforeShared = Position(*scene);
        runtime.Tick(*scene, assets, .25);
        test.Check(Position(*scene) == beforeShared + Math::Vec3(.25f,0,0) && runtime.Diagnostics().empty(),
            "Shared module versions are expanded once while active dependency cycles remain checked");
        runtime.Stop(scene);
        (void)scene->QueueRemoveScript(scene->Resolve(Actor));(void)scene->CommitPending();

        auto start=Node(1,"Start"),wait=Node(2,"Wait"),move=Node(3,"Translate");
        start.flow["next"]=wait.id;wait.number=.5;wait.flow["next"]=move.id;move.vector={2,0,0};
        BehaviorGraphAsset graph{{start,wait,move},{}};
        test.Check(ValidateBehaviorGraph(graph,error),"Behavior control flow supports explicit bounded waits");
        const auto behaviorId=AssetId::FromWords(0x4245484156494f52ULL,10);
        test.Check(assets.PublishBuiltin<BehaviorGraphAsset>(behaviorId,1,std::make_shared<const BehaviorGraphAsset>(graph),4096,error) &&
            scene->QueueSetBehaviorGraph(scene->Resolve(Actor),{behaviorId,true,{}}).IsSuccess() && scene->CommitPending().IsSuccess(),"Attach a versioned behavior program");
        BehaviorGraphRuntime behavior;
        const auto initial=Position(*scene);
        behavior.Tick(*scene,assets,.25);behavior.Tick(*scene,assets,.25);
        test.Check(Position(*scene)==initial,"Behavior Wait preserves its continuation without writing early");
        behavior.Tick(*scene,assets,.25);
        test.Check(Position(*scene)==initial+Math::Vec3(2,0,0),"Behavior continuation resumes at the accepted simulation time");
        behavior.Stop();

        // 无效变量候选不得重置上个有效实例的 Wait 与局部变量。
        const auto retainedPosition = Position(*scene);
        behavior.Tick(*scene, assets, .25);
        test.Check(scene->QueueSetBehaviorGraph(scene->Resolve(Actor), {behaviorId, true, {{"unknown", 1.0}}}).IsSuccess() &&
            scene->CommitPending().IsSuccess(), "Stage a syntactically valid but undeclared behavior variable override");
        behavior.Tick(*scene, assets, .25);
        behavior.Tick(*scene, assets, .25);
        const auto retained = behavior.Snapshot();
        test.Check(Position(*scene) == retainedPosition + Math::Vec3(2,0,0) && !retained.empty() &&
            retained.front().version == 1 && retained.front().error.find("Unknown behavior variable") != std::string::npos,
            "Rejected behavior replacements preserve the previous version and its pending continuation");
        behavior.Stop();
        (void)scene->QueueSetBehaviorGraph(scene->Resolve(Actor), {behaviorId, true, {}});
        (void)scene->CommitPending();
        auto repeat=Node(4,"Translate");repeat.vector={99,0,0};repeat.flow["next"]=repeat.id;
        start.flow["next"]=repeat.id;graph.nodes={start,repeat};
        test.Check(ValidateBehaviorGraph(graph,error) && assets.PublishBuiltin<BehaviorGraphAsset>(behaviorId,2,std::make_shared<const BehaviorGraphAsset>(graph),4096,error),
            "A behavior loop is a control-flow program rather than an invalid material DAG");
        const auto stable=Position(*scene);
        behavior.Tick(*scene,assets,.25);
        const auto snapshots=behavior.Snapshot();
        test.Check(Position(*scene)==stable && !snapshots.empty() && snapshots.front().error.find("budget")!=error.npos,
            "Behavior budget failure rolls back the entire event write set and identifies a node");
        behavior.Stop();

        auto pulse = Node(5, "Event"); pulse.event = "Pulse";
        wait.number = 100; pulse.flow["next"] = wait.id;
        graph.nodes = {pulse, wait, move};
        test.Check(ValidateBehaviorGraph(graph,error) &&
            assets.PublishBuiltin<BehaviorGraphAsset>(behaviorId,3,std::make_shared<const BehaviorGraphAsset>(graph),4096,error),
            "Publish an event-only behavior with bounded delayed continuations");
        behavior.Tick(*scene, assets, .25);
        bool queued = true;
        for (size_t count = 0; count < 64; ++count) queued = behavior.Emit(Actor, "Pulse", error) && queued;
        const auto waitingPosition = Position(*scene);
        behavior.Tick(*scene, assets, .25);
        behavior.Tick(*scene, assets, .25);
        const auto waiting = behavior.Snapshot();
        test.Check(queued && Position(*scene) == waitingPosition && !waiting.empty() && waiting.front().error.empty(),
            "A full Wait queue is legal when the graph has no automatic Update entry");
        test.Check(!behavior.Emit(Actor, "Pulse", error) && error.find("queue limit") != std::string::npos,
            "An actual event beyond the continuation capacity is rejected");
        behavior.Stop();
        (void)scene->QueueRemoveBehaviorGraph(scene->Resolve(Actor));(void)scene->CommitPending();

        (void)scenes.SetActive(handle);
        Editor::SceneDocument document(StableId::FromWords(99,1),scenes,handle);
        test.Check(document.StartPlay(assets,error) && document.PlayHandle()!=document.Handle(),"Play creates a separate scene handle");
        if(document.Playing()) {
            auto* play=scenes.GetScene(document.PlayHandle());
            test.Check(play && play->GetWorld().GetIdentity()!=scene->GetWorld().GetIdentity() && !play->Resolve(api.self).HasValue(),
                "Edit-world entity references cannot write to PlayWorld");
            const auto author=Position(*scene);
            ScriptApi playing{*play,play->MakeReference(play->Resolve(Actor)),{}};
            test.Check(playing.Translate({3,0,0},error) && playing.commands.Commit(*play,error) && Position(*scene)==author,
                "Play commands do not change author transforms");
            const auto retired=document.PlayHandle();
            test.Check(document.StopPlay(error) && !scenes.GetScene(retired) && scenes.GetActiveHandle()==handle,"Stop returns to the author scene and retires the Play handle");
        }
        auto authorCounter = std::make_shared<int>(0);
        auto playCounter = std::make_shared<std::shared_ptr<int>>();
        SystemDesc native;
        native.id = "test.independent-native";
        native.execute = [authorCounter](SystemContext&) { ++*authorCounter; return WorldStatus{}; };
        native.cloneForPlay = [playCounter](Scene&) {
            auto state = std::make_shared<int>(0);
            *playCounter = state;
            SystemDesc copy;
            copy.id = "test.independent-native";
            copy.execute = [state](SystemContext&) { ++*state; return WorldStatus{}; };
            return copy;
        };
        test.Check(scene->GetSystems().Add(native).IsSuccess() && document.StartPlay(assets, error),
            "A native Play factory installs an independent state object");
        if (document.Playing()) {
            auto* play = scenes.GetScene(document.PlayHandle());
            FrameTime time;
            RuntimeServices services;
            SystemContext context{*play, time, services};
            test.Check(play->GetSystems().RunPhase(SystemPhase::Gameplay, context).IsSuccess() &&
                *authorCounter == 0 && *playCounter && **playCounter == 1,
                "Executing the native Play system does not mutate its author's state");
            (void)document.StopPlay(error);
        }
        (void)scene->GetSystems().Clear();
        SystemDesc uncloneable;uncloneable.id="test.no-factory";uncloneable.execute=[](SystemContext&){return WorldStatus{};};
        test.Check(scene->GetSystems().Add(uncloneable).IsSuccess() && !document.StartPlay(assets,error) && scenes.GetActiveHandle()==handle,
            "A native system without an independent factory cannot silently share author state with Play");
        (void)scene->GetSystems().Clear();
    }
}
