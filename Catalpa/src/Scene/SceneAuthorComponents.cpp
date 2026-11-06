#include "catpch.h"
#include "Scene/Scene.h"
#include "Scene/Components/ScriptComponent.h"
#include "Scene/Components/BehaviorGraphComponent.h"
#include "Scene/Components/PrefabInstanceComponent.h"
#include "ECS/Detail/WorldComponentType.h"
#include "Core/Serialization/AuthorJson.h"
#include "Scripting/ScriptValueArchive.h"
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa {
    const ComponentType& ComponentTraits<ScriptComponent>::Get() {
        return Detail::MakeComponentType<ScriptComponent>();
    }
    const ComponentType& ComponentTraits<BehaviorGraphComponent>::Get() {
        return Detail::MakeComponentType<BehaviorGraphComponent>();
    }
    const ComponentType& ComponentTraits<PrefabInstanceComponent>::Get() {
        return Detail::MakeComponentType<PrefabInstanceComponent>();
    }

    WorldStatus Scene::QueueSetScript(EntityId id, const ScriptComponent& value) try {
        if (const auto status=ValidateEntity(id);!status.IsSuccess()) return status;
        std::string error;
        if (!value.script.HasValue() || !ValidateScriptValues(value.properties,error))
            return {WorldError::InvalidArgument, "Invalid script component"};
        if(m_ComponentSchemas) {
            ArchiveValue properties;
            if(!EncodeScriptValues(value.properties,properties,error)) return {WorldError::InvalidArgument,"Invalid script properties"};
            ArchiveValue fields{ArchiveValue::Object{{"schemaVersion",ArchiveValue{uint64_t(1)}},
                {"asset",ArchiveValue{value.script.ToString()}},{"enabled",ArchiveValue{value.enabled}},{"properties",std::move(properties)}}};
            if(const auto status=ValidateComponent("catalpa.script",fields);!status.IsSuccess()) return status;
        }
        return m_Commands.Enqueue([id, value](World& world) {
            return world.Has<ScriptComponent>(id) ? world.Set(id, value) : world.Add(id, value);
        });
    } catch (const std::bad_alloc&) {
        return {WorldError::AllocationFailed, "Cannot capture script component"};
    }

    WorldStatus Scene::QueueSetBehaviorGraph(EntityId id, const BehaviorGraphComponent& value) try {
        if (const auto status=ValidateEntity(id);!status.IsSuccess()) return status;
        std::string error;
        if (!value.graph.HasValue() || !ValidateBehaviorVariables(value.variables,error))
            return {WorldError::InvalidArgument, "Invalid behavior component"};
        if(m_ComponentSchemas) {
            ArchiveValue variables;
            if(!EncodeScriptValues(value.variables,variables,error)) return {WorldError::InvalidArgument,"Invalid behavior variables"};
            ArchiveValue fields{ArchiveValue::Object{{"schemaVersion",ArchiveValue{uint64_t(1)}},
                {"asset",ArchiveValue{value.graph.ToString()}},{"enabled",ArchiveValue{value.enabled}},{"variables",std::move(variables)}}};
            if(const auto status=ValidateComponent("catalpa.behavior_graph",fields);!status.IsSuccess()) return status;
        }
        return m_Commands.Enqueue([id, value](World& world) {
            return world.Has<BehaviorGraphComponent>(id) ? world.Set(id, value) : world.Add(id, value);
        });
    } catch (const std::bad_alloc&) {
        return {WorldError::AllocationFailed, "Cannot capture behavior component"};
    }

    WorldStatus Scene::QueueSetPrefabInstance(EntityId id, const PrefabInstanceComponent& value) try {
        if (const auto status=ValidateEntity(id);!status.IsSuccess()) return status;
        if (!value.source.HasValue() || !value.sourceVersion || value.entities.empty() ||
            value.entities.size() > 65536 || !AuthorJson::Parse(value.overrides).is_object())
            return {WorldError::InvalidArgument, "Invalid prefab provenance"};
        if(m_ComponentSchemas) {
            ArchiveValue::Object mapping;
            for(const auto& [local,instance]:value.entities) mapping.emplace(local.ToString(),ArchiveValue{instance.ToString()});
            std::unique_ptr<ArchiveValue> overrides;ArchiveError error;ArchiveLimits limits;
            limits.maxBytes=16*1024*1024;limits.maxStringBytes=limits.maxBytes;limits.maxNodes=1000000;limits.maxCollectionEntries=65536;
            if(!JsonArchive::Parse(value.overrides,overrides,error,limits)) return {WorldError::InvalidArgument,"Invalid prefab overrides"};
            ArchiveValue fields{ArchiveValue::Object{{"schemaVersion",ArchiveValue{uint64_t(1)}},
                {"asset",ArchiveValue{value.source.ToString()}},{"version",ArchiveValue{value.sourceVersion}},
                {"entities",ArchiveValue{std::move(mapping)}},{"overrides",std::move(*overrides)}}};
            if(const auto status=ValidateComponent("catalpa.prefab_instance",fields);!status.IsSuccess()) return status;
        }
        return m_Commands.Enqueue([id, value](World& world) {
            return world.Has<PrefabInstanceComponent>(id) ? world.Set(id, value) : world.Add(id, value);
        });
    } catch (const std::exception&) {
        return {WorldError::InvalidArgument, "Invalid prefab provenance"};
    }

    WorldStatus Scene::QueueRemoveScript(EntityId id) {
        return m_Commands.Enqueue([id](World& world) { return world.Remove<ScriptComponent>(id); });
    }
    WorldStatus Scene::QueueRemoveBehaviorGraph(EntityId id) {
        return m_Commands.Enqueue([id](World& world) { return world.Remove<BehaviorGraphComponent>(id); });
    }
    WorldStatus Scene::QueueRemovePrefabInstance(EntityId id) {
        return m_Commands.Enqueue([id](World& world) { return world.Remove<PrefabInstanceComponent>(id); });
    }
}
