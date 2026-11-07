#pragma once
#include "ECS/World.h"
namespace Catalpa {
    struct AudioListenerComponent {
        bool enabled = true;
    };
    template <> struct ComponentTraits<AudioListenerComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
