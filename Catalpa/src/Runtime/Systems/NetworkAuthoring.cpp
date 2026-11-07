#include "catpch.h"
#include "Runtime/Systems/NetworkAuthoring.h"
#include "Scene/Components/NetworkIdentityComponent.h"
#include "Scene/SceneDynamicComponents.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
    const ComponentType& ComponentTraits<NetworkIdentityComponent>::Get() {
        return Detail::MakeComponentType<NetworkIdentityComponent>();
    }
    namespace {
        const ArchiveValue& Field(const ArchiveValue& value, const char* name) {
            const auto* field = value.Find(name);
            if (!field)
                throw std::invalid_argument(std::string("Missing network identity field: ") + name);
            return *field;
        }
        NetworkIdentityComponent Read(const ArchiveValue& value) {
            const auto* object = std::get_if<ArchiveValue::Object>(&value.data);
            if (!object)
                throw std::invalid_argument("Network identity fields must be an object");
            for (const auto& [key, item] : *object)
                if (key != "schemaVersion" && key != "identity" && key != "generation" && key != "owner" &&
                    key != "enabled")
                    throw std::invalid_argument("Unknown network identity field: " + key);
            uint64_t version = 0, generation = 0, owner = 0;
            if (!Field(value, "schemaVersion").ReadUnsigned(version) || version != 1 ||
                !Field(value, "generation").ReadUnsigned(generation) || !generation ||
                !Field(value, "owner").ReadUnsigned(owner) || !owner || owner > UINT32_MAX)
                throw std::invalid_argument("Network identity version/generation/owner is invalid");
            const auto* id = std::get_if<std::string>(&Field(value, "identity").data);
            const auto* enabled = std::get_if<bool>(&Field(value, "enabled").data);
            NetworkIdentityComponent result;
            if (!id || !StableId::TryParse(*id, result.identity) || !enabled)
                throw std::invalid_argument("Invalid network stable identity or enabled field");
            result.generation = generation;
            result.owner = uint32_t(owner);
            result.enabled = *enabled;
            return result;
        }
        WorldStatus Apply(World& world, EntityId entity,
                          const std::optional<NetworkIdentityComponent>& value) {
            if (value)
                return world.Has<NetworkIdentityComponent>(entity) ? world.Set(entity, *value)
                                                                   : world.Add(entity, *value);
            return world.Has<NetworkIdentityComponent>(entity)
                       ? world.Remove<NetworkIdentityComponent>(entity)
                       : WorldStatus{};
        }
    } // namespace
    bool CaptureNetworkComponents(const World& world, EntityId entity,
                                  std::map<std::string, ArchiveValue, std::less<>>& fields,
                                  std::string& error) try {
        if (world.Has<NetworkIdentityComponent>(entity)) {
            NetworkIdentityComponent value;
            const auto status = world.Get(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            fields["catalpa.network_identity"] =
                ArchiveValue{ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
                                                  {"identity", ArchiveValue{value.identity.ToString()}},
                                                  {"generation", ArchiveValue{value.generation}},
                                                  {"owner", ArchiveValue{uint64_t(value.owner)}},
                                                  {"enabled", ArchiveValue{value.enabled}}}};
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool ValidateNetworkComponent(std::string_view type, const ArchiveValue& value, std::string& error) try {
        if (type != "catalpa.network_identity") {
            error = "Unknown network component type";
            return false;
        }
        (void)Read(value);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool RestoreNetworkComponents(World& world, EntityId entity,
                                  const std::map<std::string, ArchiveValue, std::less<>>& fields,
                                  std::string& error) try {
        std::optional<NetworkIdentityComponent> value;
        if (const auto found = fields.find("catalpa.network_identity"); found != fields.end())
            value = Read(found->second);
        const auto status = Apply(world, entity, value);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool RestoreNetworkComponents(Scene& scene, EntityId entity,
                                  const std::map<std::string, ArchiveValue, std::less<>>& fields,
                                  std::string& error) try {
        std::optional<NetworkIdentityComponent> value;
        if (const auto found = fields.find("catalpa.network_identity"); found != fields.end())
            value = Read(found->second);
        const auto status = QueueDynamicComponent(
            scene, entity, [entity, value](World& world) { return Apply(world, entity, value); });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
} // namespace Catalpa
