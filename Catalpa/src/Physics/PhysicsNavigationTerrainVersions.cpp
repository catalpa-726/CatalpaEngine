#include "catpch.h"

#include "Physics/PhysicsNavigationTerrainVersions.h"

#include "Scene/Components/ColliderComponent.h"
#include "Scene/Components/NavigationAgentComponent.h"
#include "Scene/Components/TerrainComponent.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 根据资产标识和依赖快照更新类型化的固定版本引用。
		 * 在给定依赖列表中解析指定资源并绑定其固定版本。
		 *
		 * @param[in] id 要绑定的资源标识；空标识表示不绑定。
		 * @param[in,out] target 待更新的固定版本。目标标识为空时清空；未找到依赖快照时保留标识相符的旧绑定，否则清空。
		 * @param[in] values 可用的依赖版本快照。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 依赖类型和版本一致时为 true；缺少快照可保留匹配的目标引用，类型不符或候选冲突时为 false。
		 */
		template <class T>
		bool Bind(
		    AssetId id, AssetVersionRef<T>& target, std::span<const AssetDependencyValue> values, std::string& error) {
			if (!id.HasValue()) {
				target = {};
				return true;
			}
			const AssetDependencyValue* found = nullptr;
			for (const auto& value : values)
				if (value.id == id && value.cppType != typeid(T)) {
					error = "Fixed domain dependency has the wrong asset type";
					return false;
				}
			for (const auto& value : values)
				if (value.id == id && value.cppType == typeid(T)) {
					if (found && (found->version != value.version || found->value.get() != value.value.get())) {
						error = "Ambiguous fixed domain dependency";
						return false;
					}
					found = &value;
				}
			if (!found) {
				if (target.HasValue() && target.GetId() != id)
					target = {};
				return true;
			}
			return AssetVersionRef<T>::TryCreate(
			    id, found->version, std::static_pointer_cast<const T>(found->value), target, error);
		}

		/**
		 * @brief 收集实体所持有的依赖快照，拒绝同一资产标识的版本冲突。
		 * 将身份匹配的版本快照收集到依赖列表并检查一致性。
		 *
		 * @param[in] expected 该引用应对应的资源标识。
		 * @param[in] value 调用方持有的资源版本。
		 * @param[in,out] values 已收集的固定依赖，匹配同一份载荷时复用；尚未收集时追加，版本冲突时报错。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 引用不适用、已匹配或成功加入集合时为 true，已收集项的版本或载荷冲突时为 false。
		 */
		template <class T>
		bool Collect(AssetId expected, const AssetVersionRef<T>& value, std::vector<AssetDependencyValue>& values,
		    std::string& error) {
			if (!value.HasValue() || value.GetId() != expected)
				return true;
			for (const auto& current : values)
				if (current.id == value.GetId() && current.cppType == typeid(T)) {
					if (current.version != value.GetVersion() || current.value.get() != value.Get()) {
						error = "Entity contains conflicting domain versions for the same asset ID";
						return false;
					}
					return true;
				}
			values.push_back({value.GetId(), value.GetVersion(), value.GetShared(), typeid(T)});
			return true;
		}
	} /* 命名空间 */
	bool BindPhysicsNavigationTerrainVersions(
	    World& world, EntityId entity, std::span<const AssetDependencyValue> values, std::string& error) try {
		std::optional<ColliderComponent> collider;
		std::optional<NavigationAgentComponent> agent;
		std::optional<TerrainComponent> terrain;
		if (world.Has<ColliderComponent>(entity)) {
			auto status = world.Get(entity, collider.emplace());
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Bind(collider->shape.id, collider->shapeVersion, values, error) ||
			    !Bind(collider->material.id, collider->materialVersion, values, error))
				return false;
		}
		if (world.Has<NavigationAgentComponent>(entity)) {
			auto status = world.Get(entity, agent.emplace());
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Bind(agent->mesh.id, agent->meshVersion, values, error))
				return false;
		}
		if (world.Has<TerrainComponent>(entity)) {
			auto status = world.Get(entity, terrain.emplace());
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Bind(terrain->asset.id, terrain->assetVersion, values, error))
				return false;
			const auto material =
			    terrain->assetVersion.HasValue() ? terrain->assetVersion.Get()->material.id : AssetId{};
			if (!Bind(material, terrain->materialVersion, values, error))
				return false;
		}
		/* 先检查所有候选；仅发布字段和固定引用，SDK实例仍由各Service在固定步重建。 */
		if (collider) {
			const auto status = world.Set(entity, *collider);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		if (agent) {
			const auto status = world.Set(entity, *agent);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		if (terrain) {
			const auto status = world.Set(entity, *terrain);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool CollectPhysicsNavigationTerrainVersions(
	    const World& world, EntityId entity, std::vector<AssetDependencyValue>& values, std::string& error) try {
		if (world.Has<ColliderComponent>(entity)) {
			ColliderComponent v;
			const auto status = world.Get(entity, v);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Collect(v.shape.id, v.shapeVersion, values, error) ||
			    !Collect(v.material.id, v.materialVersion, values, error))
				return false;
		}
		if (world.Has<NavigationAgentComponent>(entity)) {
			NavigationAgentComponent v;
			const auto status = world.Get(entity, v);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Collect(v.mesh.id, v.meshVersion, values, error))
				return false;
		}
		if (world.Has<TerrainComponent>(entity)) {
			TerrainComponent v;
			const auto status = world.Get(entity, v);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!Collect(v.asset.id, v.assetVersion, values, error))
				return false;
			const auto material = v.assetVersion.HasValue() ? v.assetVersion.Get()->material.id : AssetId{};
			if (!Collect(material, v.materialVersion, values, error))
				return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
} /* 命名空间 Catalpa */
