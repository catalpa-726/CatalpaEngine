#include "catpch.h"
#include "Scene/Scene.h"
#include "Core/Math/TransformMath.h"
#include "ECS/Detail/WorldComponentType.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/Components/CameraComponent.h"
#include "Camera/CameraProjection.h"


namespace Catalpa {

    // 组件描述符只在引擎内部实例化；公开头不传播 EnTT 类型。
    const ComponentType& ComponentTraits<IdentityComponent>::Get() { return Detail::MakeComponentType<IdentityComponent>(); }
    const ComponentType& ComponentTraits<TransformComponent>::Get() { return Detail::MakeComponentType<TransformComponent>(); }
    const ComponentType& ComponentTraits<RelationshipComponent>::Get() { return Detail::MakeComponentType<RelationshipComponent>(); }
    const ComponentType& ComponentTraits<CameraComponent>::Get() { return Detail::MakeComponentType<CameraComponent>(); }

    Scene::Scene(std::string name) : m_Name(std::move(name)) {}
    Scene::~Scene() = default;

    WorldStatus Scene::ValidateEntity(EntityId entity) const noexcept {
        if (entity.world != 0 && entity.world != m_World.GetIdentity()) return { WorldError::ForeignWorld, "Entity belongs to another scene world" };
        if (!m_World.IsAlive(entity)) return { WorldError::InvalidEntity, "Entity is stale or invalid" };
        return {};
    }

    WorldStatus Scene::QueueCreate(PersistentEntityId persistent, std::string_view name, const Math::TransformTRS& local, EntityId parent) {
        try {
            return m_Commands.QueueCreate([this, persistent, name = std::string(name), local, parent](World&, EntityId entity) {
                return InitializeEntity(entity, persistent, name, local, parent);
            });
        } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture entity creation" }; }
    }

    WorldStatus Scene::QueueDestroySubtree(EntityId entity) {
        try { return m_Commands.Enqueue([this, entity](World&) { return DestroySubtree(entity); }); }
        catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture subtree destruction" }; }
    }

    WorldStatus Scene::QueueSetLocal(EntityId entity, const Math::TransformTRS& local) {
        try { return m_Commands.Enqueue([this, entity, local](World&) { return SetLocal(entity, local); }); }
        catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture local transform" }; }
    }

    WorldStatus Scene::QueueSetParent(EntityId entity, EntityId parent, bool keepWorld) {
        try { return m_Commands.Enqueue([this, entity, parent, keepWorld](World&) { return SetParent(entity, parent, keepWorld); }); }
        catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture hierarchy change" }; }
    }

    WorldStatus Scene::CommitPending() {
        if (m_Committing || m_UpdatingTransforms) return { WorldError::Busy, "Scene is already committing or updating" };
        m_Committing = true;
        // Apply 自己收束命令异常；结果移动不会重新分配，保留逐命令错误供调用者检查。
        m_LastCommands = m_Commands.Apply(m_World);
        m_Committing = false;
        return m_LastCommands.status;
    }

    WorldStatus Scene::QueueSetCamera(EntityId entity, const CameraComponent& camera) {
        if (const auto status = ValidateCameraLens(camera.lens); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
        if (camera.viewId == 0 || camera.historyRevision == 0) return { WorldError::InvalidArgument, "Camera view identity and history revision must be nonzero" };
        try { return m_Commands.Enqueue([this, entity, camera](World& world) {
            if (const auto status = ValidateEntity(entity); !status.IsSuccess()) return status;
            return world.Has<CameraComponent>(entity) ? world.Set(entity, camera) : world.Add(entity, camera);
        }); } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture camera change" }; }
    }

    WorldStatus Scene::QueueRemoveCamera(EntityId entity) {
        try { return m_Commands.Enqueue([this, entity](World& world) {
            if (const auto status = ValidateEntity(entity); !status.IsSuccess()) return status;
            return world.Remove<CameraComponent>(entity);
        }); } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot capture camera removal" }; }
    }

    WorldStatus Scene::InitializeEntity(EntityId entity, PersistentEntityId persistent, const std::string& name, const Math::TransformTRS& local, EntityId parent) {
        if (!persistent.HasValue()) return { WorldError::InvalidArgument, "Persistent entity ID cannot be zero" };
        if (m_Entities.contains(persistent)) return { WorldError::AlreadyExists, "Persistent entity ID already exists in this scene" };
        if (parent != EntityId{}) {
            if (const auto status = ValidateEntity(parent); !status.IsSuccess()) return status;
        }
        Math::Mat4 localMatrix{ 1.0f };
        if (const auto status = Math::ComposeTRS(local, localMatrix); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
        if (const auto status = m_World.Add(entity, IdentityComponent{ persistent, name }); !status.IsSuccess()) return status;
        if (const auto status = m_World.Add(entity, TransformComponent{ local, Math::Mat4{ 1.0f }, false }); !status.IsSuccess()) return status;
        if (const auto status = m_World.Add(entity, RelationshipComponent{ parent }); !status.IsSuccess()) return status;
        // 最后发布稳定 ID 映射；此前任意失败由 ECB 销毁待建实体及已添加组件。
        if (!m_Entities.emplace(persistent, entity).second) return { WorldError::AlreadyExists, "Persistent entity ID already exists in this scene" };
        m_Dirty = true;
        return {};
    }

    EntityId Scene::Resolve(PersistentEntityId persistent) const noexcept {
        const auto found = m_Entities.find(persistent);
        return found != m_Entities.end() && m_World.IsAlive(found->second) ? found->second : EntityId{};
    }

    EntityId Scene::Resolve(EntityReference reference) const noexcept {
        if (!reference.HasValue() || reference.world != m_World.GetIdentity()) return {};
        const EntityId entity = Resolve(reference.persistent);
        return entity == reference.resolved ? entity : EntityId{};
    }

    EntityReference Scene::MakeReference(EntityId entity) const {
        EntityReference result;
        const auto status = m_World.Read<IdentityComponent>(entity, [&](const IdentityComponent& identity) {
            result = { m_World.GetIdentity(), identity.persistent, entity };
            return WorldStatus{};
        });
        return status.IsSuccess() ? result : EntityReference{};
    }

    WorldStatus Scene::GetTransform(EntityId entity, TransformComponent& output) const {
        TransformComponent copy;
        const auto status = m_World.Get(entity, copy);
        if (!status.IsSuccess()) return status;
        // 上游变更会使后代缓存一起过时；直到整棵层级更新成功才对外标记有效。
        if (m_Dirty) copy.worldValid = false;
        output = copy;
        return {};
    }

    WorldStatus Scene::GetRelationship(EntityId entity, RelationshipComponent& output) const { return m_World.Get(entity, output); }
    WorldStatus Scene::GetIdentity(EntityId entity, IdentityComponent& output) const { return m_World.Get(entity, output); }

    WorldStatus Scene::SetLocal(EntityId entity, const Math::TransformTRS& local) {
        if (const auto status = ValidateEntity(entity); !status.IsSuccess()) return status;
        Math::Mat4 matrix{ 1.0f };
        if (const auto status = Math::ComposeTRS(local, matrix); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
        TransformComponent value;
        if (const auto status = m_World.Get(entity, value); !status.IsSuccess()) return status;
        value.local = local;
        value.worldValid = false;
        if (const auto status = m_World.Set(entity, value); !status.IsSuccess()) return status;
        m_Dirty = true;
        return {};
    }

    WorldStatus Scene::SetParent(EntityId entity, EntityId parent, bool keepWorld) {
        if (const auto status = ValidateEntity(entity); !status.IsSuccess()) return status;
        // 从新父向上走，限制步数并验证完整 EntityId，拒绝自环、祖先环及失效父。
        EntityId ancestor = parent;
        size_t remaining = m_World.GetEntityCount();
        while (ancestor != EntityId{}) {
            if (ancestor == entity) return { WorldError::InvalidArgument, "Hierarchy change would create a cycle" };
            if (remaining-- == 0) return { WorldError::InvalidArgument, "Parent hierarchy already contains a cycle" };
            if (const auto status = ValidateEntity(ancestor); !status.IsSuccess()) return status;
            RelationshipComponent relation;
            if (const auto status = m_World.Get(ancestor, relation); !status.IsSuccess()) return status;
            ancestor = relation.parent;
        }
        RelationshipComponent previousRelation;
        TransformComponent previousTransform;
        if (const auto status = m_World.Get(entity, previousRelation); !status.IsSuccess()) return status;
        if (const auto status = m_World.Get(entity, previousTransform); !status.IsSuccess()) return status;
        if (previousRelation.parent == parent) return {};

        TransformComponent nextTransform = previousTransform;
        if (keepWorld) {
            // 按当前作者数据计算临时世界矩阵，不能为一次失败重挂接提前发布缓存。
            std::vector<TransformSystem::ComputedTransform> computed;
            if (const auto status = TransformSystem::Compute(*this, computed); !status.IsSuccess()) return status;
            Math::Mat4 oldWorld{ 1.0f }, parentWorld{ 1.0f };
            for (const auto& item : computed) {
                if (item.entity == entity) oldWorld = item.worldMatrix;
                if (item.entity == parent) parentWorld = item.worldMatrix;
            }
            Math::Mat4 inverseParent{ 1.0f }, newLocal{ 1.0f };
            if (const auto status = Math::InverseAffine(parentWorld, inverseParent); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
            if (const auto status = Math::ComposeWorld(inverseParent, oldWorld, newLocal); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
            if (const auto status = Math::DecomposeTRS(newLocal, nextTransform.local); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
        }
        nextTransform.worldValid = false;
        // 两个组件均为不分配的值；全部数学/层级检查完成后才写入。
        if (const auto status = m_World.Set(entity, nextTransform); !status.IsSuccess()) return status;
        if (const auto status = m_World.Set(entity, RelationshipComponent{ parent }); !status.IsSuccess()) {
            const auto rollback = m_World.Set(entity, previousTransform);
            return rollback.IsSuccess() ? status : rollback;
        }
        m_Dirty = true;
        return {};
    }

    WorldStatus Scene::DestroySubtree(EntityId entity) {
        if (const auto status = ValidateEntity(entity); !status.IsSuccess()) return status;
        // 先建立完整临时邻接表，所有分配和身份校验发生在首个删除之前。
        struct Node { EntityId entity; PersistentEntityId persistent; EntityId parent; };
        std::vector<Node> nodes;
        nodes.reserve(m_World.GetEntityCount());
        auto status = m_World.ForEach([&](EntityId current) {
            Node node{ current, {}, {} };
            auto read = m_World.Read<IdentityComponent>(current, [&](const IdentityComponent& identity) {
                node.persistent = identity.persistent;
                return WorldStatus{};
            });
            if (!read.IsSuccess()) return read;
            RelationshipComponent relation;
            read = m_World.Get(current, relation);
            if (!read.IsSuccess()) return read;
            node.parent = relation.parent;
            const auto mapped = m_Entities.find(node.persistent);
            if (mapped == m_Entities.end() || mapped->second != current) return WorldStatus{ WorldError::InvalidEntity, "Scene identity map is inconsistent" };
            nodes.push_back(node);
            return WorldStatus{};
        });
        if (!status.IsSuccess()) return status;

        constexpr size_t absent = std::numeric_limits<size_t>::max();
        std::map<uint32_t, size_t> indices;
        std::vector<size_t> firstChild(nodes.size(), absent), nextSibling(nodes.size(), absent), order;
        std::vector<bool> visited(nodes.size(), false);
        order.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) indices.emplace(nodes[i].entity.index, i);
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].parent == EntityId{}) continue;
            const auto parent = indices.find(nodes[i].parent.index);
            if (parent == indices.end() || nodes[parent->second].entity != nodes[i].parent) return { WorldError::InvalidEntity, "Hierarchy contains a stale or foreign parent" };
            nextSibling[i] = firstChild[parent->second];
            firstChild[parent->second] = i;
        }
        order.push_back(indices.at(entity.index));
        visited[order.front()] = true;
        for (size_t head = 0; head < order.size(); ++head) {
            for (size_t child = firstChild[order[head]]; child != absent; child = nextSibling[child]) {
                if (visited[child]) return { WorldError::InvalidArgument, "Subtree contains a hierarchy cycle" };
                visited[child] = true;
                order.push_back(child);
            }
        }
        // 反向宽度顺序先销毁子代；World 删除既存值组件和回收槽位不分配内存。
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const Node& node = nodes[*it];
            status = m_World.Destroy(node.entity);
            if (!status.IsSuccess()) return status;
            m_Entities.erase(node.persistent);
        }
        m_Dirty = true;
        return {};
    }

}
