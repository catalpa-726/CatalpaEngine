#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Core/Math/MathTypes.h"
#include "Scene/EntityReference.h"

namespace Catalpa {
    enum class PhysicsMotion : uint8_t { Static, Kinematic, Dynamic };
    enum class CollisionShapeKind : uint8_t { Box, Sphere, Capsule, ConvexHull, TriangleMesh };
    struct PhysicsBodyHandle {
        uint64_t world = 0, value = 0;
        bool HasValue() const noexcept {
            return world && value;
        }
        auto operator<=>(const PhysicsBodyHandle&) const = default;
    };
    struct PhysicsWorldConfig {
        uint32_t maxBodies = 4096, maxContacts = 8192, maxEvents = 8192;
        size_t scratchBytes = 16 * 1024 * 1024;
        Math::Vec3 gravity{0, -9.81f, 0};
        // 16 个作者碰撞组；配对必须由双方掩码同时允许。
        std::array<uint16_t, 16> collisionMasks = [] {
            std::array<uint16_t, 16> value;
            value.fill(0xffff);
            return value;
        }();
    };
    CATALPA_API bool ValidatePhysicsWorldConfig(const PhysicsWorldConfig&, std::string& error);
    struct PhysicsPose {
        Math::Vec3 position{};
        Math::Quat rotation{1, 0, 0, 0};
    };
    struct PhysicsBodyState {
        PhysicsPose pose;
        Math::Vec3 linearVelocity{};
        bool active = false;
    };
    struct PhysicsBodyDesc {
        EntityReference entity;
        PhysicsMotion motion = PhysicsMotion::Static;
        PhysicsPose pose;
        Math::Vec3 scale{1}, linearVelocity{};
        float massKg = 1, gravityFactor = 1;
        uint8_t collisionGroup = 0;
        bool sensor = false;
    };
    enum class PhysicsEventKind : uint8_t { Begin, Persist, End };
    struct PhysicsContactEvent {
        PhysicsEventKind kind = PhysicsEventKind::Begin;
        uint64_t tick = 0;
        EntityReference first, second;
        Math::Vec3 point{}, normal{};
        bool trigger = false;
        PhysicsBodyHandle firstBody, secondBody;
    };
    struct PhysicsQueryHit {
        bool hit = false;
        PhysicsBodyHandle body;
        EntityReference entity;
        Math::Vec3 point{}, normal{};
        float fraction = 1;
    };
    struct CharacterSettings {
        float radius = 0.3f, halfHeight = 0.6f, maxSlopeRadians = 0.78539816f;
        float stepHeight = 0.3f, snapDistance = 0.3f, massKg = 70;
        uint8_t collisionGroup = 0;
    };
    struct CharacterMotion {
        PhysicsPose pose;
        Math::Vec3 velocity{};
        bool grounded = false;
    };
} // namespace Catalpa
