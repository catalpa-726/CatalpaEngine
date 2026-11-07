#pragma once
#include "Physics/PhysicsTypes.h"
namespace Catalpa {
    struct PhysicsMaterialAsset {
        float friction = 0.5f, restitution = 0;
    };
    CATALPA_API bool ValidatePhysicsMaterial(const PhysicsMaterialAsset&, std::string& error);
    CATALPA_API bool ReadPhysicsMaterial(std::string_view, PhysicsMaterialAsset&, std::string& error);
    CATALPA_API bool WritePhysicsMaterial(const PhysicsMaterialAsset&, std::string&, std::string& error);
} // namespace Catalpa
