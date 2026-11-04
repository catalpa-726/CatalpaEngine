#pragma once

#include "catpch.h"
#include "Core/Id/StableId.h"

namespace Catalpa {

    // 持久化资产身份；不与场景作者 ID、路径、内容版本或运行期槽位代次隐式互转。
    struct AssetId {
        StableId value;

        constexpr AssetId() = default;
        explicit constexpr AssetId(StableId stable) noexcept : value(stable) {}
        [[nodiscard]] constexpr bool HasValue() const noexcept { return value.HasValue(); }
        [[nodiscard]] static constexpr AssetId FromWords(uint64_t high, uint64_t low) noexcept {
            return AssetId{ StableId::FromWords(high, low) };
        }

        // 复用 Core 的唯一文本协议；解析失败时保留调用方已有身份。
        [[nodiscard]] static bool TryParse(std::string_view text, AssetId& output) {
            StableId candidate;
            if (!StableId::TryParse(text, candidate)) return false;
            output = AssetId{ candidate };
            return true;
        }
        [[nodiscard]] std::string ToString() const { return value.ToString(); }
        constexpr auto operator<=>(const AssetId&) const = default;
    };

}
