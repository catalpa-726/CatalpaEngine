#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace Catalpa {

    // 稳定128位值；字节按高位在前排列。生成器、文本协议和持久化在后续阶段接入。
    struct StableId {
        std::array<uint8_t, 16> bytes{};
        [[nodiscard]] constexpr bool HasValue() const noexcept {
            for (uint8_t byte : bytes) if (byte != 0) return true;
            return false;
        }
        [[nodiscard]] static constexpr StableId FromWords(uint64_t high, uint64_t low) noexcept {
            StableId result;
            for (unsigned i = 0; i < 8; ++i) {
                result.bytes[i] = static_cast<uint8_t>(high >> (56 - i * 8));
                result.bytes[i + 8] = static_cast<uint8_t>(low >> (56 - i * 8));
            }
            return result;
        }
        constexpr auto operator<=>(const StableId&) const = default;
    };

}
