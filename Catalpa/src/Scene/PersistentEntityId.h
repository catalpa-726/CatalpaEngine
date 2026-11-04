#pragma once

#include "Core/Id/StableId.h"

namespace Catalpa {

    // 场景作者身份，不与资产ID或可复用的运行期EntityId混用。
    struct PersistentEntityId {
        StableId value;
        [[nodiscard]] constexpr bool HasValue() const noexcept { return value.HasValue(); }
        [[nodiscard]] static constexpr PersistentEntityId FromWords(uint64_t high, uint64_t low) noexcept { return { StableId::FromWords(high, low) }; }
        constexpr auto operator<=>(const PersistentEntityId&) const = default;
    };

}
