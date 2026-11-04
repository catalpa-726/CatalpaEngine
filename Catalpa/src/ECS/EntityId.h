#pragma once

#include "Core/Memory/GenerationalHandle.h"

namespace Catalpa {

    struct EntityTag;

    // 运行期实体身份：World 身份与槽位代次都必须匹配，不作为持久化 ID。
    struct EntityId {
        uint64_t world = 0;
        uint32_t index = GenerationalHandle<EntityTag>::InvalidIndex;
        uint32_t generation = 0;

        [[nodiscard]] constexpr bool HasValue() const noexcept { return world != 0 && index != GenerationalHandle<EntityTag>::InvalidIndex && generation != 0; }
        constexpr bool operator==(const EntityId&) const = default;
    };

}
