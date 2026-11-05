#include "BoxGameplay.h"
#include "Assets/AssetManager.h"
#include "Runtime/RuntimeRegistration.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialValidation.h"
#include "Materials/MaterialAssetLoader.h"

#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Geometry/PrimitiveMesh.h"
#include "Geometry/MeshValidation.h"
#include "Core/Time/FrameTime.h"
#include "Core/Math/CoordinateConvention.h"
#include "Runtime/RuntimeServices.h"
#include "Runtime/Input/InputActions.h"
#include "ECS/SystemContext.h"
#include "Log/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace Catalpa {
    namespace {
        const PersistentEntityId ParentId = PersistentEntityId::FromWords(0x5330333100000001ULL, 1);
        const PersistentEntityId ChildId = PersistentEntityId::FromWords(0x5330333100000001ULL, 2);
        const PersistentEntityId CameraId = PersistentEntityId::FromWords(0x5330333200000001ULL, 1);
        const PersistentEntityId ForegroundId = PersistentEntityId::FromWords(0x5330333300000001ULL, 1);
        const PersistentEntityId GroundId = PersistentEntityId::FromWords(0x5330333300000001ULL, 2);

        // 从受捕获/焦点控制的动作快照读取样例轴，不直接轮询平台按键。
        double Action(const ActionSnapshot* input, std::string_view id)
        {
            const auto* action = input ? input->Find(id) : nullptr;
            return action ? action->value : 0.0;
        }
    }

    struct BoxGameplay::State {
        Math::TransformTRS camera;
        double yaw = 0.0;
        double pitch = 0.0;
    };

    bool BoxGameplay::Attach(SceneManager& scenes, AssetManager& assets, bool reversedZ, bool errorMaterial)
    {
        try {
        m_Scenes = &scenes;
        m_ReportedHierarchy = false;
        m_State = std::make_shared<State>();
        m_State->camera.translation = Math::Vec3{ 0.0f, 2.0f, 10.0f };
        WorldStatus status = scenes.CreateScene("S03.2 Fixed-step Camera Sample", m_Scene);
        if (status.IsSuccess()) status = scenes.SetActive(m_Scene);
        if (!status.IsSuccess()) { CAT_ERROR("Cannot install sandbox: {0}", status.message); Detach(); return false; }
        Scene& scene = *scenes.GetScene(m_Scene);
        Math::TransformTRS parent;
        parent.translation = Math::Vec3{ 1.0f, 2.0f, 3.0f };
        const float halfAngle = std::numbers::pi_v<float> / 4.0f;
        parent.rotation = Math::Quat{ std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f };
        parent.scale = Math::Vec3{ 2.0f, 3.0f, 4.0f };
        Math::TransformTRS child;
        child.translation = Math::Vec3{ 1.0f, 0.0f, 0.0f };
        status = scene.QueueCreate(ParentId, "Parent", parent);
        if (status.IsSuccess()) status = scene.QueueCreate(ChildId, "Child", child);
        if (status.IsSuccess()) status = scene.QueueCreate(CameraId, "Sandbox Camera", m_State->camera);
        Math::TransformTRS foreground;
        foreground.translation = { -2.3f, 0.4f, 5.0f };
        foreground.scale = { 0.8f, 1.4f, 0.8f };
        Math::TransformTRS ground;
        ground.translation = { 0.0f, -0.5f, 1.0f };
        if (status.IsSuccess()) status = scene.QueueCreate(ForegroundId, "Foreground cube", foreground);
        if (status.IsSuccess()) status = scene.QueueCreate(GroundId, "Ground plane", ground);
        // Attach 发生在主循环外；此处是明确的启动结构安全点。
        if (status.IsSuccess()) status = scene.CommitPending();
        if (status.IsSuccess()) status = scene.QueueSetParent(scene.Resolve(ChildId), scene.Resolve(ParentId));
        CameraComponent camera;
        camera.lens.nearPlane = 0.1f;
        camera.lens.farPlane = 30.0f;
        camera.lens.reversedZ = reversedZ;
        if (status.IsSuccess()) status = scene.QueueSetCamera(scene.Resolve(CameraId), camera);
        // Box只生产示例内容；发布、版本和GPU派生分别交给引擎服务。
        std::string assetError; bool fallback=false;
        AssetVersionRef<MaterialInstance> material;
        if(!assets.Acquire(BuiltinMaterialId(BuiltinMaterialKind::Unlit),material,fallback,assetError)) {
            CAT_ERROR("Cannot acquire default material: {0}",assetError);Detach();return false;
        }
        const auto component=[&](const MeshResourceRef& mesh, AssetId id, const Math::Vec4& color, MeshRendererComponent& output) {
            const auto& data=mesh->GetData();
            auto value=std::make_shared<const MeshAsset>(MeshAsset{mesh,{"default"}});
            size_t resident=sizeof(MeshAsset)+sizeof(MeshResource)+data.vertexData.capacity()+data.indexData.capacity()
                +data.attributes.capacity()*sizeof(VertexAttribute)+data.submeshes.capacity()*sizeof(Submesh)
                +value->materialSlots.capacity()*sizeof(std::string);
            for(const auto& slot:value->materialSlots) resident+=slot.capacity()+1;
            if(!assets.PublishBuiltin<MeshAsset>(id,1,std::move(value),resident,assetError)) return false;
            AssetVersionRef<MeshAsset> version;
            if(!assets.Acquire(id,version,fallback,assetError)) return false;
            output={mesh,color,true,version,{material}}; return true;
        };
        MeshData cube;
        MeshResourceRef geometry;
        if (status.IsSuccess()) {
            auto meshStatus = CreateCubeMesh(1.0f, cube);
            if (meshStatus.IsSuccess()) meshStatus = CreateMeshResource(cube, geometry);
            if (!meshStatus.IsSuccess()) { CAT_ERROR("Cannot prepare sandbox geometry: {0}", meshStatus.message); Detach(); return false; }
            MeshRendererComponent cubeRenderer;
            if(!component(geometry,AssetId::FromWords(0x434154414C504142ULL,1),Math::Vec4{0.1f,0.8f,0.5f,1.0f},cubeRenderer)) {
                CAT_ERROR("Cannot publish cube: {0}",assetError);Detach();return false;
            }
            status=scene.QueueSetMeshRenderer(scene.Resolve(ParentId),cubeRenderer);
            cubeRenderer.color={0.9f,0.4f,0.2f,1.0f};
            if(status.IsSuccess()) status=scene.QueueSetMeshRenderer(scene.Resolve(ChildId),cubeRenderer);
            cubeRenderer.color={0.55f,0.25f,0.85f,1.0f};
            if(errorMaterial) {
                // 故障样例只生产一个引用缺失shader的合法材质；回退决策由Renderer统一承担。
                std::shared_ptr<const MaterialInstance> missing;
                const auto missingId=AssetId::FromWords(0x434154414C504142ULL,3);
                if(!CreateBuiltinMaterialInstance(BuiltinMaterialKind::Unlit,AssetId::FromWords(0x434154414C504142ULL,4),1,
                    AssetId::FromWords(0x434154414C504142ULL,99),missing,assetError)
                    ||!assets.PublishBuiltin<MaterialInstance>(missingId,1,missing,MaterialInstanceBytes(*missing)+MaterialDefinitionBytes(*missing->definition.Get()),assetError)) {
                    CAT_ERROR("Cannot prepare error material sample: {0}",assetError);Detach();return false;
                }
                AssetVersionRef<MaterialInstance> fixed;
                if(!assets.Acquire(missingId,fixed,fallback,assetError)) {CAT_ERROR("{0}",assetError);Detach();return false;}
                cubeRenderer.materials={fixed};
            }
            if(status.IsSuccess()) status=scene.QueueSetMeshRenderer(scene.Resolve(ForegroundId),cubeRenderer);
            MeshData plane;
            MeshResourceRef planeGeometry;
            meshStatus = CreatePlaneMesh(12.0f, 12.0f, plane);
            if (meshStatus.IsSuccess()) meshStatus = CreateMeshResource(plane, planeGeometry);
            if (!meshStatus.IsSuccess()) { CAT_ERROR("Cannot prepare ground geometry: {0}", meshStatus.message); Detach(); return false; }
            MeshRendererComponent planeRenderer;
            if(!component(planeGeometry,AssetId::FromWords(0x434154414C504142ULL,2),Math::Vec4{0.18f,0.2f,0.22f,1.0f},planeRenderer)) {
                CAT_ERROR("Cannot publish plane: {0}",assetError);Detach();return false;
            }
            if(status.IsSuccess()) status=scene.QueueSetMeshRenderer(scene.Resolve(GroundId),planeRenderer);
        }
        if (status.IsSuccess()) status = scene.CommitPending();
        if (!status.IsSuccess()) { CAT_ERROR("Cannot create sandbox content: {0}", status.message); Detach(); return false; }

        SystemDesc gameplay;
        gameplay.id = "sandbox.camera-movement";
        gameplay.phase = SystemPhase::Gameplay;
        gameplay.accesses = { { "input.actions", SystemAccessMode::Read }, { "scene.commands", SystemAccessMode::Write } };
        gameplay.execute = [state = m_State](SystemContext& context) -> WorldStatus {
            const auto* input = context.services.input;
            // 编辑导航不拥有游戏相机。没有玩法输入时也不回写缓存的旧 Transform。
            if (!input || !input->gameplayActive) return {};
            const double dx = Action(input, "look.yaw"), dy = Action(input, "look.pitch");
            state->yaw = std::remainder(state->yaw - dx * 0.0025, 2.0 * std::numbers::pi);
            // 样例相机的明确俯仰范围，避免在极点翻转；鼠标位移不乘 dt 或 DPI。
            state->pitch = std::clamp(state->pitch - dy * 0.0025, -std::numbers::pi / 2.0 + 0.01, std::numbers::pi / 2.0 - 0.01);
            const Math::Quat yaw{ static_cast<float>(std::cos(state->yaw / 2.0)), 0.0f, static_cast<float>(std::sin(state->yaw / 2.0)), 0.0f };
            const Math::Quat pitch{ static_cast<float>(std::cos(state->pitch / 2.0)), static_cast<float>(std::sin(state->pitch / 2.0)), 0.0f, 0.0f };
            state->camera.rotation = yaw * pitch;
            Math::Vec3 direction = (state->camera.rotation * Math::Vec3{ 1.0f, 0.0f, 0.0f }) * static_cast<float>(Action(input, "move.right"))
                + Math::Vec3{ 0.0f, 1.0f, 0.0f } * static_cast<float>(Action(input, "move.up"))
                + (state->camera.rotation * Math::Vec3{ 0.0f, 0.0f, -1.0f }) * static_cast<float>(Action(input, "move.forward"));
            const float distance = glm::length(direction);
            // 限制样例的合成移动方向，避免同时按多轴时超过 3 m/s；不改原始输入快照。
            if (distance > 1.0f) direction /= distance;
            state->camera.translation += direction * static_cast<float>(3.0 * context.time.fixedDeltaSeconds);
            const EntityId camera = context.scene.Resolve(CameraId);
            if (!camera.HasValue()) return { WorldError::InvalidEntity, "Sandbox camera no longer exists" };
            return context.scene.QueueSetLocal(camera, state->camera);
        };
        status = m_Module.Install(scene, std::move(gameplay));
        if (!status.IsSuccess()) { CAT_ERROR("Cannot install sandbox gameplay: {0}", status.message); Detach(); return false; }
        CAT_INFO("F6 pauses simulation; F7 advances one paused tick. In the editor, navigate in Scene Viewport.");
        CAT_INFO("Scene sample: 4 instances share 2 meshes; near=0.1m, far=30m, reversed-Z={0}.", reversedZ);
        return true;
        } catch (const std::bad_alloc&) {
            CAT_ERROR("Cannot allocate sandbox gameplay state");
            Detach();
            return false;
        }
    }

    void BoxGameplay::ReportHierarchyOnce()
    {
        if (m_ReportedHierarchy || !m_Scenes || !m_Scene.HasValue()) return;
        const Scene* scene = m_Scenes->GetScene(m_Scene);
        if (!scene) return;
        TransformComponent transform;
        const auto status = scene->GetTransform(scene->Resolve(ChildId), transform);
        if (!status.IsSuccess() || !transform.worldValid) return;

        const auto& position = transform.worldMatrix[3];
        CAT_INFO("[S03.1 sample] Child world origin = ({0}, {1}, {2}); expected (1, 2, 1).", position.x, position.y, position.z);
        m_ReportedHierarchy = true;
    }

    void BoxGameplay::Detach()
    {
        if (m_Scenes && m_Scene.HasValue()) {
            if (Scene* scene = m_Scenes->GetScene(m_Scene)) {
                const auto stopped = m_Module.Uninstall(*scene);
                if (!stopped.IsSuccess()) CAT_WARN("Gameplay registration remains until scene unload: {0}", stopped.message);
                if (const auto status = m_Scenes->Unload(m_Scene); !status.IsSuccess()) {
                    CAT_CRITICAL("Cannot safely unload sandbox scene: {0}", status.message);
                    std::abort();
                }
            }
        }
        m_Module.ForgetUnloadedScene();
        m_State.reset();
        m_Scenes = nullptr;
        m_Scene = {};
    }
}
