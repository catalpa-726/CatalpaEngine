#include "catpch.h"
#include "DomainComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Core/Serialization/JsonArchive.h"
#include "Runtime/Systems/AnimationAuthoring.h"
#include "Physics/PhysicsService.h"
#include "Navigation/NavigationService.h"
#include "Scene/AudioUIComponents.h"
#include "Physics/PhysicsNavigationTerrainVersions.h"
#include "Runtime/Systems/ParticleAuthoring.h"
#include "Terrain/TerrainService.h"
#include "Runtime/Systems/NetworkAuthoring.h"
namespace Catalpa {
    bool RemapDomainEntityReferences(ArchiveValue::Object& fields,
                                     const std::map<PersistentEntityId, PersistentEntityId>& mapping,
                                     std::string& error) {
        // 只重映射已定义的实体引用字段；资产 ID、用户字符串和网络身份不得按字符串猜测。
        const auto skin = fields.find("catalpa.skinned_mesh");
        if (skin != fields.end()) {
            auto* object = std::get_if<ArchiveValue::Object>(&skin->second.data);
            if (!object) {
                error = "Skin component is not an object";
                return false;
            }
            const auto field = object->find("animator");
            if (field != object->end())
                if (auto* text = std::get_if<std::string>(&field->second.data)) {
                    PersistentEntityId id;
                    if (!PersistentEntityId::TryParse(*text, id)) {
                        error = "Invalid animator entity reference";
                        return false;
                    }
                    if (const auto found = mapping.find(id); found != mapping.end())
                        *text = found->second.ToString();
                }
        }
        return ValidateDomainComponents(fields, error);
    }
    bool CaptureDomainComponents(const Scene& scene, EntityId entity, ArchiveValue::Object& output,
                                 std::string& error) {
        ArchiveValue::Object candidate;
        if (!CaptureAnimationComponents(scene.GetWorld(), entity, candidate, error) ||
            !CapturePhysicsComponents(scene, entity, candidate, error) ||
            !CaptureNavigationComponents(scene, entity, candidate, error) ||
            !CaptureAudioUIComponents(scene, entity, candidate, error) ||
            !CaptureParticleComponents(scene.GetWorld(), entity, candidate, error) ||
            !CaptureTerrainComponents(scene, entity, candidate, error) ||
            !CaptureNetworkComponents(scene.GetWorld(), entity, candidate, error))
            return false;
        output = std::move(candidate);
        return true;
    }
    bool ValidateDomainComponents(const ArchiveValue::Object& components, std::string& error) {
        for (const auto& [key, value] : components) {
            if (key == "catalpa.animator" || key == "catalpa.skinned_mesh") {
                if (!ValidateAnimationComponent(key, value, error))
                    return false;
            } else if (key == "rigid_body" || key == "collider" || key == "character_controller") {
                if (!ValidatePhysicsComponents({{key, value}}, error))
                    return false;
            } else if (key == "navigation_agent") {
                if (!ValidateNavigationComponents({{key, value}}, error))
                    return false;
            } else if (key == "audio_source" || key == "audio_listener" || key == "ui") {
                if (!ValidateAudioUIComponent(key, value, error))
                    return false;
            } else if (key == "catalpa.particle_emitter") {
                if (!ValidateParticleComponent(key, value, error))
                    return false;
            } else if (key == "terrain") {
                if (!ValidateTerrainComponents({{key, value}}, error))
                    return false;
            } else if (key == "catalpa.network_identity") {
                if (!ValidateNetworkComponent(key, value, error))
                    return false;
            } else {
                error = "Unknown scene domain component: " + key;
                return false;
            }
        }
        if (!ValidatePhysicsComponents(components, error))
            return false;
        error.clear();
        return true;
    }
    template <class Owner>
    bool Restore(Owner& owner, EntityId entity, const ArchiveValue::Object& components, std::string& error) {
        if (!ValidateDomainComponents(components, error) ||
            !RestoreAnimationComponents(owner, entity, components, error) ||
            !RestorePhysicsComponents(owner, entity, components, error) ||
            !RestoreNavigationComponents(owner, entity, components, error) ||
            !RestoreParticleComponents(owner, entity, components, error) ||
            !RestoreTerrainComponents(owner, entity, components, error) ||
            !RestoreNetworkComponents(owner, entity, components, error))
            return false;
        for (const auto& [key, value] : components)
            if (key == "audio_source" || key == "audio_listener" || key == "ui")
                if (const auto status = RestoreAudioUIComponent(owner, entity, key, value, error);
                    !status.IsSuccess())
                    return false;
        return true;
    }
    bool RestoreDomainComponents(Scene& scene, EntityId entity, const ArchiveValue::Object& data,
                                 std::string& error) {
        return Restore(scene, entity, data, error);
    }
    bool RestoreDomainComponents(World& world, EntityId entity, const ArchiveValue::Object& data,
                                 std::string& error) {
        return Restore(world, entity, data, error);
    }
    std::vector<AssetId> DomainComponentDependencies(const ArchiveValue::Object& components) {
        std::set<AssetId> ids;
        auto add = [&](const auto& values) {
            for (auto id : values)
                if (id.HasValue())
                    ids.insert(id);
        };
        add(TerrainComponentDependencies(components));
        add(PhysicsComponentDependencies(components));
        add(NavigationComponentDependencies(components));
        for (const auto& [key, value] : components) {
            if (key == "catalpa.particle_emitter")
                add(ParticleComponentDependencies(key, value));
            if (key == "catalpa.animator" || key == "catalpa.skinned_mesh")
                add(AnimationComponentDependencies(key, value));
            if (key == "audio_source" || key == "audio_listener" || key == "ui") {
                std::vector<AssetId> values;
                std::string error;
                if (!AudioUIComponentDependencies(key, value, values, error))
                    throw std::invalid_argument(error);
                add(values);
            }
        }
        return {ids.begin(), ids.end()};
    }
    bool CollectDomainComponentVersions(const World& world, EntityId entity,
                                        std::vector<AssetDependencyValue>& output, std::string& error) {
        std::vector<AssetDependencyValue> values;
        if (!CollectAnimationComponentVersions(world, entity, values, error) ||
            !CollectParticleComponentVersions(world, entity, values, error) ||
            !CollectAudioUIComponentVersions(world, entity, values, error) ||
            !CollectPhysicsNavigationTerrainVersions(world, entity, values, error))
            return false;
        output = std::move(values);
        return true;
    }
    bool BindDomainComponentVersions(World& world, EntityId entity,
                                     std::span<const AssetDependencyValue> values, std::string& error) {
        if (values.empty()) {
            error.clear();
            return true;
        }
        return BindAnimationComponentVersions(world, entity, values, error) &&
               BindParticleComponentVersions(world, entity, values, error) &&
               BindAudioUIComponentVersions(world, entity, values, error) &&
               BindPhysicsNavigationTerrainVersions(world, entity, values, error);
    }
    bool BindDomainComponentVersions(Scene& scene, EntityId entity, std::vector<AssetDependencyValue> values,
                                     std::string& error) {
        const auto status =
            QueueDynamicComponent(scene, entity, [entity, values = std::move(values)](World& world) {
                std::string diagnostic;
                return BindDomainComponentVersions(world, entity, values, diagnostic)
                           ? WorldStatus{}
                           : WorldStatus{WorldError::InvalidArgument, "Cannot bind fixed domain versions"};
            });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    }
    bool SameDomainComponents(const ArchiveValue::Object& left, const ArchiveValue::Object& right) {
        std::string a, b;
        ArchiveError error;
        return JsonArchive::Write(ArchiveValue{left}, a, error) &&
               JsonArchive::Write(ArchiveValue{right}, b, error) && a == b;
    }
} // namespace Catalpa
