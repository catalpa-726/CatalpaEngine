#include "BoxGameplay.h"

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
#include "Renderer/RenderFramePacket.h"
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

    bool BoxGameplay::Attach(SceneManager& scenes)
    {
        try {
        m_Scenes = &scenes;
        m_ReportSeconds = 0.0;
        m_ReportedHierarchy = false;
        m_State = std::make_shared<State>();
        m_State->camera.translation = Math::Vec3{ 0.0f, 2.0f, 6.0f };
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
        // Attach 发生在主循环外；此处是明确的启动结构安全点。
        if (status.IsSuccess()) status = scene.CommitPending();
        if (status.IsSuccess()) status = scene.QueueSetParent(scene.Resolve(ChildId), scene.Resolve(ParentId));
        if (status.IsSuccess()) status = scene.QueueSetCamera(scene.Resolve(CameraId), CameraComponent{});
        // 一个引擎生成并密封的立方体供两个实体共享；Box不创建或拥有GPU buffer。
        MeshData cube;
        MeshResourceRef geometry;
        if (status.IsSuccess()) {
            auto meshStatus = CreateCubeMesh(1.0f, cube);
            if (meshStatus.IsSuccess()) meshStatus = CreateMeshResource(cube, geometry);
            if (!meshStatus.IsSuccess()) { CAT_ERROR("Cannot prepare sandbox geometry: {0}", meshStatus.message); Detach(); return false; }
            status = scene.QueueSetMeshRenderer(scene.Resolve(ParentId), { geometry, Math::Vec4{ 0.1f, 0.8f, 0.5f, 1.0f }, true });
            if (status.IsSuccess()) status = scene.QueueSetMeshRenderer(scene.Resolve(ChildId), { geometry, Math::Vec4{ 0.9f, 0.4f, 0.2f, 1.0f }, true });
        }
        if (status.IsSuccess()) status = scene.CommitPending();
        if (!status.IsSuccess()) { CAT_ERROR("Cannot create sandbox content: {0}", status.message); Detach(); return false; }

        SystemDesc gameplay;
        gameplay.id = "sandbox.camera-movement";
        gameplay.phase = SystemPhase::Gameplay;
        gameplay.accesses = { { "input.actions", SystemAccessMode::Read }, { "scene.commands", SystemAccessMode::Write } };
        gameplay.execute = [state = m_State](SystemContext& context) -> WorldStatus {
            const auto* input = context.services.input;
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
        CAT_INFO("[S03.2] WASD/QE move, hold right mouse to look, F6 pause, F7 step while paused; UI capture suppresses gameplay.");
        CAT_INFO("Geometry snapshot sample: two instances share one sealed cube. GPU depth/UBO drawing is pending the next batch.");
        return true;
        } catch (const std::bad_alloc&) {
            CAT_ERROR("Cannot allocate sandbox gameplay state");
            Detach();
            return false;
        }
    }

    void BoxGameplay::Update(const FrameTime& time, const RenderFramePacket& frame)
    {
        if (!m_Scenes || !m_Scene.HasValue()) return;
        const Scene* scene = m_Scenes->GetScene(m_Scene);
        if (!scene) return;
        if (!m_ReportedHierarchy) {
            TransformComponent transform;
            if (const auto status = scene->GetTransform(scene->Resolve(ChildId), transform); status.IsSuccess() && transform.worldValid) {
                const auto& position = transform.worldMatrix[3];
                CAT_INFO("[S03.1 sample] Child world origin = ({0}, {1}, {2}); expected (1, 2, 1).", position.x, position.y, position.z);
                m_ReportedHierarchy = true;
            }
        }
        m_ReportSeconds += time.realDeltaSeconds;
        if (m_ReportSeconds < 1.0 || frame.views.empty() || frame.sceneIdentity != scene->GetWorld().GetIdentity()) return;
        m_ReportSeconds = 0.0;
        const auto& position = frame.views.front().matrices.position;
        const bool sharedGeometry = frame.instances.size() == 2 && frame.instances[0].mesh == frame.instances[1].mesh;
        CAT_INFO("Geometry snapshot: {0} instances, shared mesh = {1}", frame.instances.size(), sharedGeometry);
        CAT_INFO("[S03.2] tick={0}, simulation={1}s, steps={2}, alpha={3}, camera=({4}, {5}, {6})",
            time.tickCount == 0 ? time.firstTick - 1 : time.firstTick + time.tickCount - 1, time.simulationSeconds, time.tickCount,
            time.interpolationAlpha, position.x, position.y, position.z);
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
