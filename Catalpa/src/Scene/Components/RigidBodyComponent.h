#pragma once
#include "ECS/World.h"
#include "Physics/PhysicsTypes.h"
namespace Catalpa {
    struct RigidBodyComponent {
        PhysicsMotion motion = PhysicsMotion::Static;
        Math::Vec3 initialVelocity{}; // 世界坐标系，米/秒；dynamic运行姿态由Physics独占。
        float massKg = 1, gravityFactor = 1;
        uint64_t teleportRevision = 0;
        uint8_t collisionGroup = 0;
        bool enabled = true;
    };
    template <> struct ComponentTraits<RigidBodyComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
