#include "catpch.h"
#include "TestRunner.h"
#include "ECS/ComponentRegistry.h"
#include "Runtime/RuntimeRegistration.h"
#include "Core/Serialization/JsonArchive.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"

namespace Catalpa::Tests {
    // 执行真实Runtime注册与归档值树验证，不将反射schema当作C++对象内存布局。
    void RunComponentRegistryTests(TestContext& test) {
        ComponentRegistry registry;std::string error;
        const bool registered = RegisterRuntimeComponentSchemas(registry, error);
        constexpr std::array required{ "catalpa.identity", "catalpa.transform", "catalpa.relationship",
            "catalpa.mesh_renderer", "catalpa.light", "catalpa.environment", "catalpa.camera",
            "catalpa.script", "catalpa.behavior_graph", "catalpa.prefab_instance" };
        test.Check(registered && registry.GetSchemas().size() == required.size()
            && std::all_of(required.begin(), required.end(), [&](const auto* id) { return registry.Find(id) != nullptr; }),
            "Runtime registers each supported author component schema");
        const auto count = registry.GetSchemas().size();
        const auto* retained=registry.Find("catalpa.transform");
        test.Check(!RegisterRuntimeComponentSchemas(registry,error)&&registry.GetSchemas().size()==count
            &&registry.Find("catalpa.transform")==retained,"Duplicate batch registration preserves existing schema owners");
        const auto validate=[&](std::string_view id,std::string_view json) {
            std::unique_ptr<ArchiveValue> value;ArchiveError parse;
            return JsonArchive::Parse(json,value,parse)&&registry.Validate(id,*value,error);
        };
        test.Check(validate("catalpa.transform",R"({"schemaVersion":1,"translation":[1,2,3],"rotation":[1,0,0,0],"scale":[-1,2,1]})"),"TRS schema keeps wxyz quaternion and valid negative scale");
        test.Check(!validate("catalpa.transform",R"({"schemaVersion":1,"translation":[1,2,3],"rotation":[2,0,0,0],"scale":[1,1,1]})"),"Runtime schema rejects nonunit quaternion without normalizing");
        test.Check(!validate("catalpa.transform",R"({"schemaVersion":1,"translation":[1,2],"rotation":[1,0,0,0],"scale":[1,1,1]})"),"Component vectors require exact shape");
        test.Check(!validate("catalpa.transform",R"({"schemaVersion":1,"translation":[1e-200,0,0],"rotation":[1,0,0,0],"scale":[1,1,1]})"),"Float32 component conversion cannot silently underflow a nonzero value");
        test.Check(!validate("catalpa.transform",R"({"schemaVersion":1,"translation":[1,2,3],"rotation":[1,0,0,0],"scale":[1,1,1],"worldMatrix":[]})"),"Runtime-only fields cannot enter persistent component schemas");
        test.Check(validate("catalpa.relationship",R"({"schemaVersion":1})"),"Absent relationship parent explicitly represents a root");
        test.Check(!validate("catalpa.relationship",R"({"schemaVersion":1,"parent":"00000000-0000-0000-0000-000000000000"})"),"A present parent must be a nonzero stable identity");
        test.Check(!validate("catalpa.relationship",R"({"schemaVersion":2})"),"Unknown component schema version is rejected");
        test.Check(!validate("catalpa.identity",R"({"schemaVersion":1,"persistent_id":"11111111-2222-3333-4444-555555555555"})"),"Required component author fields cannot disappear");
        test.Check(!validate("unregistered.component",R"({"schemaVersion":1})"),"Unregistered component type produces an explicit diagnostic");
        ComponentSchema numeric{"sample.number",1,{{"amount","Amount",ComponentFieldKind::Number,true,0.0,1.0}}, {}};
        const std::array batch{numeric,numeric};
        test.Check(!registry.RegisterMany(batch,error)&&!registry.Find("sample.number"),"Duplicate new schemas roll back the complete batch");
        test.Check(registry.Register(numeric,error),"A valid independent schema can be registered");
        test.Check(validate("sample.number",R"({"schemaVersion":1,"amount":0.5})"),"Component numeric field accepts its declared range");
        test.Check(!validate("sample.number",R"({"schemaVersion":1,"amount":true})"),"Boolean is not coerced to a numeric component field");
        test.Check(!validate("sample.number",R"({"schemaVersion":1,"amount":2})"),"Component numeric range is enforced");
        // Product path: schema ownership reaches real Scene command gates, not only a standalone validator.
        auto runtimeSchemas=std::make_shared<ComponentRegistry>();
        test.Check(RegisterRuntimeComponentSchemas(*runtimeSchemas,error),"Scene runtime schemas registered before manager publication");
        std::weak_ptr<const ComponentRegistry> schemaLifetime=runtimeSchemas;
        SceneManager scenes;
        test.Check(!scenes.SetComponentSchemas({}).IsSuccess(),"SceneManager refuses a null schema installation");
        WorldStatus otherThread;
        std::thread wrongThread([&]{otherThread=scenes.SetComponentSchemas(runtimeSchemas);});wrongThread.join();
        test.Check(otherThread.code==WorldError::Busy,"SceneManager schema installation stays on the creation thread");
        test.Check(scenes.SetComponentSchemas(runtimeSchemas).IsSuccess(),"SceneManager accepts shared runtime schema ownership before scenes exist");
        SceneHandle handle;test.Check(scenes.CreateScene("Schema scene",handle).IsSuccess(),"Scene constructed with installed author schemas");
        auto* scene=scenes.GetScene(handle);runtimeSchemas.reset();
        test.Check(!schemaLifetime.expired(),"SceneManager/Scene keep schemas alive after caller releases its owner");
        test.Check(!scenes.SetComponentSchemas(std::make_shared<ComponentRegistry>()).IsSuccess(),"Existing scenes prevent silent schema replacement");
        if(scene){
            const auto persistent=PersistentEntityId::FromWords(0x1234,0x5678);Math::TransformTRS local;
            auto invalidLocal=local;invalidLocal.rotation.w=2.0f;
            test.Check(!scene->QueueCreate(persistent,"Rejected",invalidLocal).IsSuccess()&&!scene->GetLastComponentDiagnostic().empty(),"Scene QueueCreate enforces runtime TRS schema before queuing");
            test.Check(scene->CommitPending().IsSuccess()&&scene->GetLastCommandResults().commands.empty()&&scene->GetWorld().GetEntityCount()==0,"Schema rejection leaves no partial create command/entity");
            test.Check(scene->QueueCreate(persistent,"Valid",local).IsSuccess()&&scene->CommitPending().IsSuccess(),"Valid typed author fields pass the schema gate and create once");
            const auto entity=scene->Resolve(persistent);
            invalidLocal=local;invalidLocal.translation.x=std::numeric_limits<float>::infinity();
            test.Check(!scene->QueueSetLocal(entity,invalidLocal).IsSuccess(),"Scene QueueSetLocal rejects nonfinite author fields before queuing");
            TransformComponent retainedTransform;
            test.Check(scene->CommitPending().IsSuccess()&&scene->GetLastCommandResults().commands.empty()&&scene->GetTransform(entity,retainedTransform).IsSuccess()&&retainedTransform.local.translation.x==local.translation.x,"Rejected typed update leaves previous transform and command queue intact");
        }
        test.Check(scenes.Clear().IsSuccess(),"Schema-enabled scene lifecycle closes cleanly");
        auto missing=std::make_shared<ComponentRegistry>();
        test.Check(scenes.SetComponentSchemas(missing).IsSuccess()&&scenes.CreateScene("Missing schemas",handle).IsSuccess(),"An explicit incomplete schema collection can be diagnosed by the Scene consumer");
        scene=scenes.GetScene(handle);
        if(scene){test.Check(!scene->QueueCreate(PersistentEntityId::FromWords(1,1),"Missing",{}).IsSuccess(),"Real Scene refuses creation when identity schema is missing");
            test.Check(scene->CommitPending().IsSuccess()&&scene->GetLastCommandResults().commands.empty()&&scene->GetWorld().GetEntityCount()==0,"Missing component schema never creates a partial entity");}
        test.Check(scenes.Clear().IsSuccess(),"Incomplete-schema Scene cleanup does not require a World copy");
        auto incompatible=std::make_shared<ComponentRegistry>();
        ComponentSchema wrongIdentity{"catalpa.identity",1,{{"persistent_id","ID",ComponentFieldKind::UInt,true},{"name","Name",ComponentFieldKind::String,true}}, {}};
        test.Check(incompatible->Register(wrongIdentity,error)&&scenes.SetComponentSchemas(incompatible).IsSuccess()&&scenes.CreateScene("Wrong field",handle).IsSuccess(),"A structurally registered incompatible schema is supplied to the actual Scene gate");
        scene=scenes.GetScene(handle);
        if(scene){test.Check(!scene->QueueCreate(PersistentEntityId::FromWords(2,2),"Wrong",{}).IsSuccess()&&scene->GetLastComponentDiagnostic().find("persistent_id")!=std::string::npos,"Scene typed stable identity cannot be coerced to a numeric schema field");
            test.Check(scene->CommitPending().IsSuccess()&&scene->GetLastCommandResults().commands.empty(),"Wrong author field type leaves no queued command");}
        ComponentRegistry moved=std::move(registry);
        test.Check(moved.Find("catalpa.transform")==retained&&registry.GetSchemas().empty(),"Moving registry preserves schema ownership and leaves source empty");
    }
}
