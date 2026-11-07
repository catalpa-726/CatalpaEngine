#include "catpch.h"
#include "Physics/PhysicsNavigationTerrainVersions.h"
#include "Scene/Components/ColliderComponent.h"
#include "Scene/Components/NavigationAgentComponent.h"
#include "Scene/Components/TerrainComponent.h"

namespace Catalpa {
    namespace {
        template <class T>
        bool Bind(AssetId id, AssetVersionRef<T>& target, std::span<const AssetDependencyValue> values,
                  std::string& error) {
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
                    if (found &&
                        (found->version != value.version || found->value.get() != value.value.get())) {
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
        template <class T>
        bool Collect(AssetId expected, const AssetVersionRef<T>& value,
                     std::vector<AssetDependencyValue>& values, std::string& error) {
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
    } // namespace
    bool BindPhysicsNavigationTerrainVersions(World& world, EntityId entity,
                                              std::span<const AssetDependencyValue> values,
                                              std::string& error) try {
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
        // 先检查所有候选；仅发布字段和固定引用，SDK实例仍由各Service在固定步重建。
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
    bool CollectPhysicsNavigationTerrainVersions(const World& world, EntityId entity,
                                                 std::vector<AssetDependencyValue>& values,
                                                 std::string& error) try {
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
} // namespace Catalpa
