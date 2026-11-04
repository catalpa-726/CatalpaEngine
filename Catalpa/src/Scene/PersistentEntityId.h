#pragma once

#include "Core/Id/StableId.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa {

    // 场景作者身份，不与资产ID或可复用的运行期EntityId混用。
    struct PersistentEntityId {
        StableId value;
        [[nodiscard]] constexpr bool HasValue() const noexcept { return value.HasValue(); }
        [[nodiscard]] static constexpr PersistentEntityId FromWords(uint64_t high, uint64_t low) noexcept { return { StableId::FromWords(high, low) }; }
        [[nodiscard]] std::string ToString() const { return value.ToString(); }
        [[nodiscard]] static bool TryParse(std::string_view text, PersistentEntityId& output) noexcept {
            StableId id;
            if (!StableId::TryParse(text, id)) return false;
            output = PersistentEntityId{ id };
            return true;
        }
        constexpr auto operator<=>(const PersistentEntityId&) const = default;
    };

    // 只有创建新的作者实体才调用；恢复已保存实体应解析并保留原ID。
    [[nodiscard]] inline bool GeneratePersistentEntityId(PersistentEntityId& output, std::string& error) {
        StableId id;
        if (!GenerateStableId(id, error)) return false;
        output = PersistentEntityId{ id };
        return true;
    }

}
