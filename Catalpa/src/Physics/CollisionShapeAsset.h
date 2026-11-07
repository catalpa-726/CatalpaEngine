#pragma once
#include "Physics/PhysicsTypes.h"
#include "Assets/AssetHandle.h"
namespace Catalpa {
    struct CollisionShapeAsset {
        CollisionShapeKind kind = CollisionShapeKind::Box;
        // Box 为半尺寸；Sphere 使用 x 半径；Capsule 使用 x 半径、y 柱体半高，单位米。
        Math::Vec3 dimensions{0.5f};
        std::vector<Math::Vec3> vertices;
        std::vector<uint32_t> indices;
    };
    CATALPA_API bool ValidateCollisionShape(const CollisionShapeAsset&, std::string& error);
    CATALPA_API bool ReadCollisionShape(std::string_view, CollisionShapeAsset&, std::string& error);
    CATALPA_API bool WriteCollisionShape(const CollisionShapeAsset&, std::string&, std::string& error);
} // namespace Catalpa
