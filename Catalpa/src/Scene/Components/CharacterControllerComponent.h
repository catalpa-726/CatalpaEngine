#pragma once
#include "ECS/World.h"
#include "Physics/PhysicsTypes.h"
namespace Catalpa {
    struct CharacterControllerComponent {
        CharacterSettings settings;
        // X/Z为持续的世界水平速度（米/秒）；Y为一次向上跳跃速度，消费后清零。
        Math::Vec3 desiredVelocity{};
        uint64_t teleportRevision = 0;
        bool enabled = true;
    };
    template <> struct ComponentTraits<CharacterControllerComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
