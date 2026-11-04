#include "BoxGameplay.h"

#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Core/Application.h"
#include "Log/Log.h"
#include <cmath>
#include <numbers>

namespace Catalpa {
    namespace {
        // 固定样例身份只属于此独立场景，不用运行期 entity 编号生成持久 ID。
        const PersistentEntityId ParentId = PersistentEntityId::FromWords(0x5330333100000001ULL, 1);
        const PersistentEntityId ChildId = PersistentEntityId::FromWords(0x5330333100000001ULL, 2);
    }

    bool BoxGameplay::Attach(SceneManager& scenes)
    {
        m_Scenes = &scenes;
        m_Stage = 0;
        WorldStatus status = scenes.CreateScene("S03.1 Transform Sample", m_Scene);
        if (status.IsSuccess()) status = scenes.SetActive(m_Scene);
        if (!status.IsSuccess()) {
            CAT_ERROR("Cannot install S03.1 sample: {0}", status.message);
            Detach();
            return false;
        }
        Scene* scene = scenes.GetScene(m_Scene);
        Math::TransformTRS parent;
        parent.translation = Math::Vec3{ 1.0f, 2.0f, 3.0f };
        const float halfAngle = std::numbers::pi_v<float> / 4.0f;
        parent.rotation = Math::Quat{ std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f };
        parent.scale = Math::Vec3{ 2.0f, 3.0f, 4.0f };
        Math::TransformTRS child;
        child.translation = Math::Vec3{ 1.0f, 0.0f, 0.0f };
        status = scene->QueueCreate(ParentId, "Parent", parent);
        if (status.IsSuccess()) status = scene->QueueCreate(ChildId, "Child", child);
        if (!status.IsSuccess()) {
            CAT_ERROR("Cannot queue S03.1 sample: {0}", status.message);
            Detach();
            return false;
        }
        return true;
    }

    void BoxGameplay::Update()
    {
        if (!m_Scenes || m_Stage == 2) return;
        Scene* scene = m_Scenes->GetScene(m_Scene);
        if (!scene) { Application::Get().RequestClose(); return; }
        const EntityId parent = scene->Resolve(ParentId);
        const EntityId child = scene->Resolve(ChildId);
        // 第一次主线程安全点尚未提交创建时，等待下一帧，禁止使用未发布实体。
        if (!parent.HasValue() || !child.HasValue()) return;
        if (m_Stage == 0) {
            const WorldStatus status = scene->QueueSetParent(child, parent);
            if (!status.IsSuccess()) {
                CAT_ERROR("Cannot queue S03.1 hierarchy: {0}", status.message);
                Application::Get().RequestClose();
            }
            m_Stage = 1;
            return;
        }
        TransformComponent transform;
        const WorldStatus status = scene->GetTransform(child, transform);
        if (!status.IsSuccess() || !transform.worldValid) {
            CAT_ERROR("S03.1 sample world transform was not published: {0}", status.message);
            Application::Get().RequestClose();
            return;
        }
        const auto& position = transform.worldMatrix[3];
        CAT_INFO("[S03.1 sample] Child world origin = ({0}, {1}, {2}); expected (1, 2, 1). CPU scene only.", position.x, position.y, position.z);
        m_Stage = 2;
    }

    void BoxGameplay::Detach()
    {
        if (m_Scenes && m_Scene.HasValue()) {
            const WorldStatus status = m_Scenes->Unload(m_Scene);
            if (!status.IsSuccess()) CAT_ERROR("Cannot unload S03.1 sample: {0}", status.message);
        }
        m_Scenes = nullptr;
        m_Scene = {};
        m_Stage = 0;
    }
}
