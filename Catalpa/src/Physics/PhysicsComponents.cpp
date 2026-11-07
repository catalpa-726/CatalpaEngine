#include "catpch.h"
#include "Physics/PhysicsService.h"
#include "Scene/SceneDynamicComponents.h"
#include "ECS/Detail/WorldComponentType.h"
#include "Core/Serialization/AuthorJson.h"
namespace Catalpa {
    const ComponentType& ComponentTraits<RigidBodyComponent>::Get() {
        return Detail::MakeComponentType<RigidBodyComponent>();
    }
    const ComponentType& ComponentTraits<ColliderComponent>::Get() {
        return Detail::MakeComponentType<ColliderComponent>();
    }
    const ComponentType& ComponentTraits<CharacterControllerComponent>::Get() {
        return Detail::MakeComponentType<CharacterControllerComponent>();
    }
    namespace {
        using AuthorJson::Json;
        ArchiveValue Archive(const Json& doc) {
            std::unique_ptr<ArchiveValue> value;
            ArchiveError error;
            if (!JsonArchive::Parse(doc.dump(), value, error))
                throw std::invalid_argument(error.ToString());
            return std::move(*value);
        }
        Json JsonOf(const ArchiveValue& value) {
            std::string text;
            ArchiveError error;
            if (!JsonArchive::Write(value, text, error))
                throw std::invalid_argument(error.ToString());
            return AuthorJson::Parse(text);
        }
        RigidBodyComponent Body(const ArchiveValue& value) {
            const auto doc = JsonOf(value);
            AuthorJson::Fields(doc, {"motion", "massKg", "gravityFactor", "initialVelocity", "collisionGroup",
                                     "teleportRevision", "enabled"});
            RigidBodyComponent body;
            const auto motion = AuthorJson::Unsigned(doc.at("motion"));
            const auto group = AuthorJson::Unsigned(doc.at("collisionGroup"));
            body.massKg = float(AuthorJson::Number(doc.at("massKg")));
            body.gravityFactor = float(AuthorJson::Number(doc.at("gravityFactor")));
            if (motion > 2 || group > 15 || body.massKg < 0.001f || body.massKg > 1e8 ||
                body.gravityFactor < 0 || body.gravityFactor > 100)
                throw std::invalid_argument("Invalid body fields");
            body.motion = PhysicsMotion(motion);
            body.collisionGroup = uint8_t(group);
            body.initialVelocity = AuthorJson::Vec3(doc.at("initialVelocity"));
            if (std::max({std::abs(body.initialVelocity.x), std::abs(body.initialVelocity.y),
                          std::abs(body.initialVelocity.z)}) > 10000)
                throw std::invalid_argument("Initial velocity exceeds 10 km/s bound");
            body.teleportRevision = AuthorJson::Unsigned(doc.at("teleportRevision"));
            body.enabled = doc.at("enabled").get<bool>();
            return body;
        }
        ColliderComponent Collider(const ArchiveValue& value) {
            const auto doc = JsonOf(value);
            AuthorJson::Fields(doc, {"shape", "material", "sensor"});
            ColliderComponent c;
            c.shape.id = AuthorJson::Identity<AssetId>(doc.at("shape"));
            if (doc.at("material") != "")
                c.material.id = AuthorJson::Identity<AssetId>(doc.at("material"));
            c.sensor = doc.at("sensor").get<bool>();
            return c;
        }
        CharacterControllerComponent Character(const ArchiveValue& value) {
            const auto d = JsonOf(value);
            AuthorJson::Fields(d, {"radius", "halfHeight", "slopeRadians", "stepHeight", "snapDistance",
                                   "massKg", "collisionGroup", "desiredVelocity", "teleportRevision",
                                   "enabled"});
            CharacterControllerComponent c;
            auto& s = c.settings;
            s.radius = float(AuthorJson::Number(d.at("radius")));
            s.halfHeight = float(AuthorJson::Number(d.at("halfHeight")));
            s.maxSlopeRadians = float(AuthorJson::Number(d.at("slopeRadians")));
            s.stepHeight = float(AuthorJson::Number(d.at("stepHeight")));
            s.snapDistance = float(AuthorJson::Number(d.at("snapDistance")));
            s.massKg = float(AuthorJson::Number(d.at("massKg")));
            const auto group = AuthorJson::Unsigned(d.at("collisionGroup"));
            if (s.radius < 0.01 || s.radius > 100 || s.halfHeight < 0.01 || s.halfHeight > 100 ||
                s.maxSlopeRadians < 0 || s.maxSlopeRadians >= 1.5707 || s.stepHeight < 0 ||
                s.stepHeight > 10 || s.snapDistance < 0 || s.snapDistance > 10 || s.massKg < 0.001f ||
                s.massKg > 10000 || group > 15)
                throw std::invalid_argument("Invalid character fields");
            s.collisionGroup = uint8_t(group);
            c.desiredVelocity = AuthorJson::Vec3(d.at("desiredVelocity"));
            if (c.desiredVelocity.y < 0 || std::max({std::abs(c.desiredVelocity.x), c.desiredVelocity.y,
                                                     std::abs(c.desiredVelocity.z)}) > 10000)
                throw std::invalid_argument(
                    "Character intent requires bounded horizontal speed and nonnegative jump pulse");
            c.teleportRevision = AuthorJson::Unsigned(d.at("teleportRevision"));
            c.enabled = d.at("enabled").get<bool>();
            return c;
        }
        Json Encode(const RigidBodyComponent& v) {
            return {{"motion", uint32_t(v.motion)},
                    {"massKg", v.massKg},
                    {"gravityFactor", v.gravityFactor},
                    {"initialVelocity", AuthorJson::Array(v.initialVelocity)},
                    {"collisionGroup", v.collisionGroup},
                    {"teleportRevision", v.teleportRevision},
                    {"enabled", v.enabled}};
        }
        Json Encode(const ColliderComponent& v) {
            return {{"shape", v.shape.id.ToString()},
                    {"material", v.material.HasValue() ? v.material.id.ToString() : ""},
                    {"sensor", v.sensor}};
        }
        Json Encode(const CharacterControllerComponent& v) {
            const auto& s = v.settings;
            return {{"radius", s.radius},
                    {"halfHeight", s.halfHeight},
                    {"slopeRadians", s.maxSlopeRadians},
                    {"stepHeight", s.stepHeight},
                    {"snapDistance", s.snapDistance},
                    {"massKg", s.massKg},
                    {"collisionGroup", s.collisionGroup},
                    {"desiredVelocity", AuthorJson::Array(v.desiredVelocity)},
                    {"teleportRevision", v.teleportRevision},
                    {"enabled", v.enabled}};
        }
    } // namespace
    WorldStatus SetRigidBody(Scene& scene, EntityId id, const RigidBodyComponent& value) {
        std::string error;
        try {
            Body(Archive(Encode(value)));
        } catch (...) {
            return {WorldError::InvalidArgument, "Invalid rigid body"};
        }
        return QueueDynamicComponent(scene, id, [id, value](World& w) {
            return w.Has<RigidBodyComponent>(id) ? w.Set(id, value) : w.Add(id, value);
        });
    }
    WorldStatus SetCollider(Scene& scene, EntityId id, const ColliderComponent& value) {
        if (!value.shape.HasValue())
            return {WorldError::InvalidArgument, "Collider requires a shape asset"};
        return QueueDynamicComponent(scene, id, [id, value](World& w) {
            return w.Has<ColliderComponent>(id) ? w.Set(id, value) : w.Add(id, value);
        });
    }
    WorldStatus SetCharacterController(Scene& scene, EntityId id, const CharacterControllerComponent& value) {
        try {
            Character(Archive(Encode(value)));
        } catch (...) {
            return {WorldError::InvalidArgument, "Invalid character controller"};
        }
        return QueueDynamicComponent(scene, id, [id, value](World& w) {
            return w.Has<CharacterControllerComponent>(id) ? w.Set(id, value) : w.Add(id, value);
        });
    }
    bool CapturePhysicsComponents(const Scene& scene, EntityId id, DynamicComponentMap& values,
                                  std::string& error) try {
        const auto& w = scene.GetWorld();
        if (w.Has<RigidBodyComponent>(id)) {
            RigidBodyComponent v;
            if (!w.Get(id, v).IsSuccess())
                return false;
            values["rigid_body"] = Archive(Encode(v));
        }
        if (w.Has<ColliderComponent>(id)) {
            ColliderComponent v;
            if (!w.Get(id, v).IsSuccess())
                return false;
            values["collider"] = Archive(Encode(v));
        }
        if (w.Has<CharacterControllerComponent>(id)) {
            CharacterControllerComponent v;
            if (!w.Get(id, v).IsSuccess())
                return false;
            values["character_controller"] = Archive(Encode(v));
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool ValidatePhysicsComponents(const DynamicComponentMap& values, std::string& error) try {
        if (auto i = values.find("rigid_body"); i != values.end())
            Body(i->second);
        if (auto i = values.find("collider"); i != values.end())
            Collider(i->second);
        if (auto i = values.find("character_controller"); i != values.end())
            Character(i->second);
        if (values.contains("character_controller") && values.contains("rigid_body"))
            throw std::invalid_argument("Character and rigid body cannot share transform authority");
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool RestorePhysicsComponents(Scene& scene, EntityId id, const DynamicComponentMap& values,
                                  std::string& error) try {
        if (!ValidatePhysicsComponents(values, error))
            return false;
        WorldStatus status;
        if (auto i = values.find("rigid_body"); i != values.end())
            status = SetRigidBody(scene, id, Body(i->second));
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (auto i = values.find("collider"); i != values.end())
            status = SetCollider(scene, id, Collider(i->second));
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (auto i = values.find("character_controller"); i != values.end())
            status = SetCharacterController(scene, id, Character(i->second));
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool RestorePhysicsComponents(World& world, EntityId id, const DynamicComponentMap& values,
                                  std::string& error) try {
        if (!ValidatePhysicsComponents(values, error))
            return false;
        auto apply = [&]<class T>(const T& value) {
            return world.Has<T>(id) ? world.Set(id, value) : world.Add(id, value);
        };
        if (auto i = values.find("rigid_body"); i != values.end()) {
            const auto status = apply(Body(i->second));
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        if (auto i = values.find("collider"); i != values.end()) {
            const auto status = apply(Collider(i->second));
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        if (auto i = values.find("character_controller"); i != values.end()) {
            const auto status = apply(Character(i->second));
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
    std::vector<AssetId> PhysicsComponentDependencies(const DynamicComponentMap& values) {
        std::vector<AssetId> out;
        if (auto i = values.find("collider"); i != values.end()) {
            auto c = Collider(i->second);
            out.push_back(c.shape.id);
            if (c.material.HasValue())
                out.push_back(c.material.id);
        }
        return out;
    }
} // namespace Catalpa
