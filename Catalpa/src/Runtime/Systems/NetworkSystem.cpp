#include "catpch.h"
#include "Runtime/Systems/NetworkSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/NetworkIdentityComponent.h"

namespace Catalpa {
    NetworkSystem::NetworkSystem(std::unique_ptr<ReplicationService> replication)
        : m_Replication(std::move(replication)) {
        if (!m_Replication)
            throw std::invalid_argument("NetworkSystem requires an explicit replication service");
    }
    void NetworkSystem::Stop() noexcept {
        m_Replication->Close();
        m_Remote.clear();
        m_Generations.clear();
        m_Spawned.clear();
        m_World = 0;
    }
    bool NetworkSystem::Tick(Scene& scene, uint64_t tick, std::string& error) try {
        const auto world = scene.GetWorld().GetIdentity();
        if (m_World && m_World != world) {
            error = "A network system cannot be reused with another live World";
            return false;
        }
        m_World = world;
        if (!scene.CanEditNow()) {
            error = "Network reconciliation requires an idle Scene safe point";
            return false;
        }
        const auto& config = m_Replication->Config();
        std::vector<ReplicatedEntityState> entities;
        std::string diagnostic;
        const auto scanned = scene.GetWorld().ForEach([&](EntityId entity) {
            if (!scene.GetWorld().Has<NetworkIdentityComponent>(entity))
                return WorldStatus{};
            NetworkIdentityComponent identity;
            auto status = scene.GetWorld().Get(entity, identity);
            if (!status.IsSuccess())
                return status;
            if (!identity.enabled ||
                (config.localPeer != config.serverPeer && identity.owner != config.localPeer))
                return WorldStatus{};
            RelationshipComponent relationship;
            status = scene.GetRelationship(entity, relationship);
            if (!status.IsSuccess())
                return status;
            if (relationship.parent.HasValue()) {
                diagnostic = "Replication v1 supports root entities; hierarchy requires an explicit "
                             "network-parent schema";
                return WorldStatus{WorldError::InvalidArgument, "Replicated entity has a parent"};
            }
            TransformComponent transform;
            IdentityComponent name;
            status = scene.GetTransform(entity, transform);
            if (!status.IsSuccess())
                return status;
            status = scene.GetIdentity(entity, name);
            if (!status.IsSuccess())
                return status;
            ReplicatedEntityState value;
            value.id = identity.identity;
            value.persistent = scene.MakeReference(entity).persistent;
            value.generation = identity.generation;
            value.owner = identity.owner;
            value.tick = tick;
            value.local = transform.local;
            value.name = name.name;
            if (scene.GetWorld().Has<MeshRendererComponent>(entity)) {
                MeshRendererComponent renderer;
                status = scene.GetWorld().Get(entity, renderer);
                if (!status.IsSuccess())
                    return status;
                value.visible = renderer.visible;
            }
            entities.push_back(std::move(value));
            return WorldStatus{};
        });
        if (!scanned.IsSuccess()) {
            error = diagnostic.empty() ? scanned.message : diagnostic;
            return false;
        }
        if (!m_Replication->Publish(entities, tick, error) || !m_Replication->Receive(error))
            return false;
        const auto check = [&](WorldStatus status) {
            if (!status.IsSuccess())
                error = status.message;
            return status.IsSuccess();
        };
        const auto& state = m_Replication->Entities();
        // 先提交旧代次的显式删除，再恢复新代次；运行时 EntityId 从不进入网络包。
        for (const auto& [id, reference] : m_Remote)
            if (!state.contains(id) || m_Generations.at(id) != state.at(id).generation) {
                const auto entity = scene.Resolve(reference);
                if (entity.HasValue() && m_Spawned.contains(id) && !check(scene.QueueDestroySubtree(entity)))
                    return false;
            }
        if (!check(scene.CommitPending()))
            return false;
        std::map<StableId, EntityReference> references;
        std::map<StableId, uint64_t> generations;
        for (const auto& [id, value] : state) {
            auto entity = scene.Resolve(value.persistent);
            if (!entity.HasValue()) {
                if (!check(scene.QueueCreate(value.persistent, value.name, value.local)) ||
                    !check(scene.CommitPending()))
                    return false;
                entity = scene.Resolve(value.persistent);
                m_Spawned.insert(id);
                NetworkIdentityComponent identity;
                identity.identity = id;
                identity.generation = value.generation;
                identity.owner = value.owner;
                if (!check(QueueDynamicComponent(scene, entity, [entity, identity](World& target) {
                        return target.Add(entity, identity);
                    })))
                    return false;
                if (m_Renderer) {
                    auto renderer = *m_Renderer;
                    renderer.visible = value.visible;
                    if (!check(scene.QueueSetMeshRenderer(entity, renderer)))
                        return false;
                }
            } else {
                NetworkIdentityComponent identity;
                if (!scene.GetWorld().Has<NetworkIdentityComponent>(entity) ||
                    !check(scene.GetWorld().Get(entity, identity)) || identity.identity != id ||
                    identity.generation != value.generation) {
                    error = "Replicated spawn conflicts with a local persistent identity or generation";
                    return false;
                }
                identity.owner = value.owner;
                if (!check(QueueDynamicComponent(scene, entity, [entity, identity](World& target) {
                        return target.Set(entity, identity);
                    })))
                    return false;
            }
            if (!check(scene.QueueSetLocal(entity, value.local)) ||
                !check(scene.QueueSetName(entity, value.name)))
                return false;
            if (scene.GetWorld().Has<MeshRendererComponent>(entity)) {
                MeshRendererComponent renderer;
                if (!check(scene.GetWorld().Get(entity, renderer)))
                    return false;
                renderer.visible = value.visible;
                if (!check(scene.QueueSetMeshRenderer(entity, renderer)))
                    return false;
            }
            references.emplace(id, scene.MakeReference(entity));
            generations.emplace(id, value.generation);
        }
        if (!check(scene.CommitPending()))
            return false;
        m_Remote.swap(references);
        m_Generations.swap(generations);
        std::erase_if(m_Spawned, [&](StableId id) { return !m_Remote.contains(id); });
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
} // namespace Catalpa
