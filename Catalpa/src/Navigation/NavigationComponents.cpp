#include "catpch.h"
#include "Navigation/NavigationService.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "ECS/Detail/WorldComponentType.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa {
    const ComponentType& ComponentTraits<NavigationAgentComponent>::Get() {
        return Detail::MakeComponentType<NavigationAgentComponent>();
    }
    namespace {
        ArchiveValue Encode(const NavigationAgentComponent& v) {
            const auto text = AuthorJson::Json{
                {"mesh", v.mesh.id.ToString()},
                {"destination", AuthorJson::Array(v.destination)},
                {"speed", v.speed},
                {"stoppingDistance", v.stoppingDistance},
                {"requestRevision", v.requestRevision},
                {"enabled",
                 v.enabled}}.dump();
            std::unique_ptr<ArchiveValue> out;
            ArchiveError e;
            if (!JsonArchive::Parse(text, out, e))
                throw std::invalid_argument(e.ToString());
            return std::move(*out);
        }
        NavigationAgentComponent Decode(const ArchiveValue& archive) {
            std::string text;
            ArchiveError error;
            if (!JsonArchive::Write(archive, text, error))
                throw std::invalid_argument(error.ToString());
            const auto j = AuthorJson::Parse(text);
            AuthorJson::Fields(
                j, {"mesh", "destination", "speed", "stoppingDistance", "requestRevision", "enabled"});
            NavigationAgentComponent v;
            v.mesh.id = AuthorJson::Identity<AssetId>(j.at("mesh"));
            v.destination = AuthorJson::Vec3(j.at("destination"));
            v.speed = float(AuthorJson::Number(j.at("speed")));
            v.stoppingDistance = float(AuthorJson::Number(j.at("stoppingDistance")));
            v.requestRevision = AuthorJson::Unsigned(j.at("requestRevision"));
            v.enabled = j.at("enabled").get<bool>();
            if (v.speed < 0 || v.speed > 100 || v.stoppingDistance < 0.01f || v.stoppingDistance > 100 ||
                !v.requestRevision)
                throw std::invalid_argument("Invalid navigation agent speed, stopping distance or revision");
            return v;
        }
    } // namespace
    WorldStatus SetNavigationAgent(Scene& scene, EntityId entity, const NavigationAgentComponent& value) {
        try {
            Decode(Encode(value));
        } catch (...) {
            return {WorldError::InvalidArgument, "Invalid navigation agent"};
        }
        return QueueDynamicComponent(scene, entity, [entity, value](World& w) {
            return w.Has<NavigationAgentComponent>(entity) ? w.Set(entity, value) : w.Add(entity, value);
        });
    }
    bool CaptureNavigationComponents(const Scene& scene, EntityId entity, DynamicComponentMap& values,
                                     std::string& error) try {
        if (scene.GetWorld().Has<NavigationAgentComponent>(entity)) {
            NavigationAgentComponent v;
            auto status = scene.GetWorld().Get(entity, v);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            values["navigation_agent"] = Encode(v);
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool ValidateNavigationComponents(const DynamicComponentMap& values, std::string& error) try {
        if (const auto i = values.find("navigation_agent"); i != values.end())
            Decode(i->second);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool RestoreNavigationComponents(Scene& scene, EntityId entity, const DynamicComponentMap& values,
                                     std::string& error) try {
        if (const auto i = values.find("navigation_agent"); i != values.end()) {
            const auto status = SetNavigationAgent(scene, entity, Decode(i->second));
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
    bool RestoreNavigationComponents(World& world, EntityId entity, const DynamicComponentMap& values,
                                     std::string& error) try {
        if (const auto i = values.find("navigation_agent"); i != values.end()) {
            const auto value = Decode(i->second);
            const auto status = world.Has<NavigationAgentComponent>(entity) ? world.Set(entity, value)
                                                                            : world.Add(entity, value);
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
    std::vector<AssetId> NavigationComponentDependencies(const DynamicComponentMap& values) {
        if (const auto i = values.find("navigation_agent"); i != values.end())
            return {Decode(i->second).mesh.id};
        return {};
    }
} // namespace Catalpa
