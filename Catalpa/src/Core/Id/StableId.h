#pragma once

#include "catpch.h"

namespace Catalpa {

    // 稳定128位值；字节按高位在前排列。零值无效，文本不借用主机字节序。
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

        // 唯一写出形式为小写8-4-4-4-12；旧的任意非零128位身份仍可往返。
        [[nodiscard]] std::string ToString() const {
            constexpr char hex[] = "0123456789abcdef";
            std::string text;
            text.reserve(36);
            for (size_t i = 0; i < bytes.size(); ++i) {
                if (i == 4 || i == 6 || i == 8 || i == 10) text.push_back('-');
                text.push_back(hex[bytes[i] >> 4]);
                text.push_back(hex[bytes[i] & 15]);
            }
            return text;
        }

        // 接受大小写十六进制，拒绝空身份、额外字符/括号/空格；失败保留output。
        [[nodiscard]] static bool TryParse(std::string_view text, StableId& output) noexcept {
            if (text.size() != 36) return false;
            auto nibble = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            StableId candidate;
            size_t cursor = 0;
            for (size_t i = 0; i < candidate.bytes.size(); ++i) {
                if (i == 4 || i == 6 || i == 8 || i == 10) {
                    if (text[cursor++] != '-') return false;
                }
                const int high = nibble(text[cursor++]);
                const int low = nibble(text[cursor++]);
                if (high < 0 || low < 0) return false;
                candidate.bytes[i] = static_cast<uint8_t>((high << 4) | low);
            }
            if (!candidate.HasValue()) return false;
            output = candidate;
            return true;
        }
        constexpr auto operator<=>(const StableId&) const = default;
    };

}
