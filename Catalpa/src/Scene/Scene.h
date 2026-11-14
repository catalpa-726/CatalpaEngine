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
	class SceneSubtreeSnapshot;
	struct LightComponent;
	struct EnvironmentComponent;
	struct ScriptComponent;
	struct BehaviorGraphComponent;
	struct PrefabInstanceComponent;

	/* 独占World及其结构命令；本步主线程同步操作，不承担资产加载或GPU资源。 */
	class CATALPA_API Scene {
	public:
		explicit Scene(std::string name, std::shared_ptr<const ComponentRegistry> componentSchemas = {});
		~Scene();
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
		[[nodiscard]] const std::string& GetLastComponentDiagnostic() const noexcept { return m_ComponentDiagnostic; }
		[[nodiscard]] const World& GetWorld() const noexcept { return m_World; }
		[[nodiscard]] const ComponentRegistry* GetComponentSchemas() const noexcept { return m_ComponentSchemas.get(); }
		[[nodiscard]] SystemScheduler& GetSystems() noexcept { return m_Systems; }
		[[nodiscard]] const SystemScheduler& GetSystems() const noexcept { return m_Systems; }

		/**
		 * @brief 将实体的持久标识、名称、局部变换和父节点捕获到创建命令中。
		 * 命令捕获独占值；父节点须在该命令执行时存在。提交前不发布新实体身份。
		 *
		 * @param[in] persistent 新实体要保留的持久标识。
		 * @param[in] name 新实体的作者名称。
		 * @param[in] local 相对父节点的初始平移、旋转和缩放。
		 * @param[in] parent 新实体的父节点，空值表示场景根。
		 *
		 * @return 创建命令已入队时返回成功；命令执行阶段仍会检查父节点等条件。
		 */
		[[nodiscard]] WorldStatus QueueCreate(PersistentEntityId persistent, std::string_view name,
		    const Math::TransformTRS& local, EntityId parent = {});
		/**
		 * @brief 将实体及其后代的删除操作加入命令队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueDestroySubtree(EntityId entity);
		/**
		 * @brief 将实体的局部变换修改排入队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] local 将在提交命令时写入实体的父相对变换。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetLocal(EntityId entity, const Math::TransformTRS& local);
		/**
		 * @brief 将修改实体显示名称的操作写入结构命令队列。
		 * 仅替换作者显示名称，持久实体身份及其查找映射由 Scene 保持。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] name 待写入实体身份组件的新名称。
		 *
		 * @return 命令已入队时返回成功；输入非法或入队失败时返回错误。
		 */
		[[nodiscard]] WorldStatus QueueSetName(EntityId entity, std::string_view name);
		/**
		 * @brief 将实体父节点的变更加入命令队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] parent 父节点或父实体。
		 * @param[in] keepWorld 调整父子关系时是否保持世界变换。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetParent(EntityId entity, EntityId parent, bool keepWorld = false);
		/**
		 * @brief 将实体相机组件的添加或替换操作排队到场景安全点。
		 * 可选相机组件也走同一个结构安全点，不向客户端暴露可变 World。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] camera 通过校验后排队写入目标实体的相机组件值。
		 *
		 * @return 命令成功入队时返回正常状态，否则返回校验或排队错误。
		 */
		[[nodiscard]] WorldStatus QueueSetCamera(EntityId entity, const CameraComponent& camera);
		/**
		 * @brief 将移除相机的操作排入队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueRemoveCamera(EntityId entity);
		/**
		 * @brief 校验网格、材质和显示参数，并将网格渲染器更新排入场景命令队列。
		 * 密封几何引用与显示参数由值捕获，在同一结构安全点增加或替换/移除组件。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] renderer 包含网格、材质及细节层级信息的候选网格渲染组件。
		 *
		 * @return 命令成功入队时返回正常状态；资源绑定、数据或队列无效时返回错误。
		 */
		[[nodiscard]] WorldStatus QueueSetMeshRenderer(EntityId entity, const MeshRendererComponent& renderer);
		/**
		 * @brief 将移除网格渲染器的操作排入队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueRemoveMeshRenderer(EntityId entity);
		/**
		 * @brief 将设置光源的操作排入队列。
		 *
		 * @param[in] entity 用于定位目标的实体标识。
		 * @param[in] light 光源参数。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetLight(EntityId entity, const LightComponent&);
		/**
		 * @brief 将设置环境的操作排入队列。
		 *
		 * @param[in] entity 用于定位目标的实体标识。
		 * @param[in] environment 待通过场景命令更新的环境设置。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetEnvironment(EntityId entity, const EnvironmentComponent&);
		[[nodiscard]] WorldStatus QueueRemoveLight(EntityId);
		[[nodiscard]] WorldStatus QueueRemoveEnvironment(EntityId);
		/**
		 * @brief 将设置脚本的操作排入队列。
		 *
		 * @param[in] id 用于定位目标的实体标识。
		 * @param[in] value 待挂接到实体的脚本及其属性。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetScript(EntityId, const ScriptComponent&);
		/**
		 * @brief 将设置行为图的操作排入队列。
		 *
		 * @param[in] id 用于定位目标的实体标识。
		 * @param[in] value 待挂接到实体的行为图与实例设置。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetBehaviorGraph(EntityId, const BehaviorGraphComponent&);
		/**
		 * @brief 将设置预制体实例的操作排入队列。
		 *
		 * @param[in] id 用于定位目标的实体标识。
		 * @param[in] value 待写入实体的预制体实例记录。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus QueueSetPrefabInstance(EntityId, const PrefabInstanceComponent&);
		[[nodiscard]] WorldStatus QueueRemoveScript(EntityId);
		[[nodiscard]] WorldStatus QueueRemoveBehaviorGraph(EntityId);
		[[nodiscard]] WorldStatus QueueRemovePrefabInstance(EntityId);
		/**
		 * @brief 在主线程编辑安全点收集指定实体及其后代的完整快照。
		 * 文档命令在主线程安全点独占执行；无损捕获失败时保留原实体。
		 *
		 * @param[in] root 遍历起点的实体标识。
		 * @param[out] output 成功时写入可用于撤销操作的完整子树快照。
		 *
		 * @return 子树及其所有组件都可捕获时返回成功，否则返回世界或组件错误。
		 */
		[[nodiscard]] WorldStatus CaptureForUndo(EntityId root, SceneSubtreeSnapshot& output) const;
		/**
		 * @brief 移除用于撤销。
		 *
		 * @param[in] root 遍历起点的实体标识。
		 * @param[out] output 接收 SceneSubtreeSnapshot 结果。
		 * @param[in] expected 用于比较的 SceneSubtreeSnapshot。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus RemoveForUndo(
		    EntityId root, SceneSubtreeSnapshot& output, const SceneSubtreeSnapshot* expected = nullptr);
		/**
		 * @brief 校验快照来源、父实体代次和持久标识，恢复已删除的子树。
		 * 仅文档自己恢复过父实体时可显式传入已校验的代次；默认要求捕获时的父实体仍有效。
		 *
		 * @param[in] snapshot 要恢复到当前场景的子树快照。
		 * @param[in] reboundParent 重新绑定后预期的父实体句柄，含代次。省略时使用快照记录的父实体句柄来检查父节点是否仍有效。
		 *
		 * @return 所有节点和组件恢复成功时返回正常状态，否则返回世界错误，并尝试回滚已恢复的节点。
		 */
		[[nodiscard]] WorldStatus RestoreForUndo(const SceneSubtreeSnapshot& snapshot, EntityId reboundParent = {});
		[[nodiscard]] bool CanEditNow() const noexcept {
			return !m_Committing && !m_UpdatingTransforms && !m_World.IsAccessActive() && !m_Systems.IsRunning() &&
			    m_Commands.Size() == 0;
		}
		/**
		 * @brief 在场景安全点按入队顺序提交待处理命令并保存逐项结果。
		 * FIFO逐命令原子操作，不承诺整批回滚；保留全部结果，返回首个失败。
		 *
		 * @return 全部成功时返回正常状态，否则返回首个失败状态；当前不能提交时返回 Busy。
		 */
		[[nodiscard]] WorldStatus CommitPending();
		[[nodiscard]] const EntityCommandApplyResult& GetLastCommandResults() const noexcept { return m_LastCommands; }

		/**
		 * @brief 在当前场景中解析实体的持久标识。
		 * 仅解析当前Scene；删除、卸载及跨World令牌不能返回旧实体。
		 *
		 * @param[in] persistent 要解析为当前运行时实体句柄的持久标识。
		 *
		 * @return 仍存活的实体句柄；标识无法解析或实体已销毁时返回空句柄。
		 */
		[[nodiscard]] EntityId Resolve(PersistentEntityId persistent) const noexcept;
		/**
		 * @brief 通过持久实体标识查询场景当前的运行时实体。
		 *
		 * @param[in] reference 含来源场景信息的实体引用，用于定位仍有效的实体。
		 *
		 * @return 当前世界、持久标识及运行时代次都匹配的实体句柄；任何一项失效时返回空句柄。
		 */
		[[nodiscard]] EntityId Resolve(EntityReference reference) const noexcept;
		/**
		 * @brief 将当前实体的持久身份与带代次的运行时句柄绑定在同一引用中。
		 *
		 * @param[in] entity 操作所针对的实体。
		 *
		 * @return 包含世界标识、持久标识和当前实体句柄的引用；无法读出身份组件时返回空引用。
		 */
		[[nodiscard]] EntityReference MakeReference(EntityId entity) const;
		/**
		 * @brief 返回变换。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[out] output 接收 TransformComponent 结果。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus GetTransform(EntityId entity, TransformComponent& output) const;
		[[nodiscard]] WorldStatus GetRelationship(EntityId entity, RelationshipComponent& output) const;
		[[nodiscard]] WorldStatus GetIdentity(EntityId entity, IdentityComponent& output) const;

	private:
		friend class DynamicComponentAccess;
		friend class TransformSystem;
		friend class ModelInstantiation;
		friend class ScriptCommands;
		friend class PrefabReconciler;
		[[nodiscard]] WorldStatus ValidateEntity(EntityId entity) const noexcept;
		[[nodiscard]] WorldStatus InitializeEntity(EntityId entity, PersistentEntityId persistent,
		    const std::string& name, const Math::TransformTRS& local, EntityId parent);
		[[nodiscard]] WorldStatus DestroySubtree(EntityId entity);
		[[nodiscard]] WorldStatus SetLocal(EntityId entity, const Math::TransformTRS& local);
		[[nodiscard]] WorldStatus SetParent(EntityId entity, EntityId parent, bool keepWorld);
		[[nodiscard]] WorldStatus ValidateComponent(std::string_view type, const ArchiveValue& fields);

		std::string m_Name;
		std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
		/* 最近一次作者字段的诊断由本场景持有，避免 WorldStatus 借用临时字符串。 */
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
