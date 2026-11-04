#pragma once

#include "Scene/PersistentEntityId.h"
#include "ECS/World.h"
#include <string>

namespace Catalpa {

    // 显式作者ID和名称；Scene在创建安全点检查唯一性。
    struct IdentityComponent {
        PersistentEntityId persistent;
        std::string name;
    };
    template<> struct ComponentTraits<IdentityComponent> {
        [[nodiscard]] static CATALPA_API const ComponentType& Get();
    };

}
