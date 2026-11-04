#pragma once

#include "catpch.h"

#include "ECS/EntityCommandBuffer.h"
#include "Scene/EntityReference.h"
#include "Scene/Components/IdentityComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/RelationshipComponent.h"
#include "ECS/SystemScheduler.h"

namespace Catalpa {

    class TransformSystem;
    class ComponentRegistry;
    struct ArchiveValue;
    struct CameraComponent;
    struct MeshRendererComponent;

    // 独占World及其结构命令；本步主线程同步操作，不承担资产加载或GPU资源。
    class CATALPA_API Scene {
    public:
        explicit Scene(std::string name, std::shared_ptr<const ComponentRegistry> componentSchemas = {});
        ~Scene();
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
        // 最近一次作者字段schema诊断由Scene持有，不在WorldStatus内借用临时字符串。
        [[nodiscard]] const std::string& GetLastComponentDiagnostic() const noexcept { return m_ComponentDiagnostic; }
        [[nodiscard]] const World& GetWorld() const noexcept { return m_World; }
        // 调度器由此 Scene 独占；调用方只在场景有效期间短期借用。
        [[nodiscard]] SystemScheduler& GetSystems() noexcept { return m_Systems; }
        [[nodiscard]] const SystemScheduler& GetSystems() const noexcept { return m_Systems; }

        // 命令捕获独占值；父节点须在该命令执行时存在。提交前不发布新实体身份。
        [[nodiscard]] WorldStatus QueueCreate(PersistentEntityId persistent, std::string_view name, const Math::TransformTRS& local, EntityId parent = {});
        [[nodiscard]] WorldStatus QueueDestroySubtree(EntityId entity);
        [[nodiscard]] WorldStatus QueueSetLocal(EntityId entity, const Math::TransformTRS& local);
        [[nodiscard]] WorldStatus QueueSetParent(EntityId entity, EntityId parent, bool keepWorld = false);
        // 可选相机组件也走同一个结构安全点，不向客户端暴露可变 World。
        [[nodiscard]] WorldStatus QueueSetCamera(EntityId entity, const CameraComponent& camera);
        [[nodiscard]] WorldStatus QueueRemoveCamera(EntityId entity);
        // 密封几何引用与显示参数由值捕获，在同一结构安全点增加或替换/移除组件。
        [[nodiscard]] WorldStatus QueueSetMeshRenderer(EntityId entity, const MeshRendererComponent& renderer);
        [[nodiscard]] WorldStatus QueueRemoveMeshRenderer(EntityId entity);
        // FIFO逐命令原子操作，不承诺整批回滚；保留全部结果，返回首个失败。
        [[nodiscard]] WorldStatus CommitPending();
        [[nodiscard]] const EntityCommandApplyResult& GetLastCommandResults() const noexcept { return m_LastCommands; }

        // 仅解析当前Scene；删除、卸载及跨World令牌不能返回旧实体。
        [[nodiscard]] EntityId Resolve(PersistentEntityId persistent) const noexcept;
        [[nodiscard]] EntityId Resolve(EntityReference reference) const noexcept;
        [[nodiscard]] EntityReference MakeReference(EntityId entity) const;
        [[nodiscard]] WorldStatus GetTransform(EntityId entity, TransformComponent& output) const;
        [[nodiscard]] WorldStatus GetRelationship(EntityId entity, RelationshipComponent& output) const;
        [[nodiscard]] WorldStatus GetIdentity(EntityId entity, IdentityComponent& output) const;

    private:
        friend class TransformSystem;
        [[nodiscard]] WorldStatus ValidateEntity(EntityId entity) const noexcept;
        [[nodiscard]] WorldStatus InitializeEntity(EntityId entity, PersistentEntityId persistent, const std::string& name, const Math::TransformTRS& local, EntityId parent);
        [[nodiscard]] WorldStatus DestroySubtree(EntityId entity);
        [[nodiscard]] WorldStatus SetLocal(EntityId entity, const Math::TransformTRS& local);
        [[nodiscard]] WorldStatus SetParent(EntityId entity, EntityId parent, bool keepWorld);
        [[nodiscard]] WorldStatus ValidateComponent(std::string_view type, const ArchiveValue& fields);

        std::string m_Name;
        std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
        std::string m_ComponentDiagnostic;
        World m_World;
        SystemScheduler m_Systems;
        EntityCommandBuffer m_Commands;
        EntityCommandApplyResult m_LastCommands;
        std::map<PersistentEntityId, EntityId> m_Entities;
        bool m_Dirty = false;
        bool m_Committing = false;
        bool m_UpdatingTransforms = false;
    };

}
