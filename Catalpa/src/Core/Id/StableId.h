#pragma once

#include "catpch.h"

namespace Catalpa {

	/*
	 * 稳定128位值；字节按高位在前排列。零值无效，文本不借用主机字节序。
	 * 文本写为小写 8-4-4-4-12 格式，读取兼容大写；解析失败保留原值。
	 */
	struct StableId {
		std::array<uint8_t, 16> bytes{};
		/**
		 * @brief 检查此引用或值是否有效。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		[[nodiscard]] constexpr bool HasValue() const noexcept {
			for (uint8_t byte : bytes)
				if (byte != 0)
					return true;
			return false;
		}
		/**
		 * @brief 由组成标识的整数构造标识值。
		 *
		 * @param[in] high 标识的高 64 位。
		 * @param[in] low 标识的低 64 位。
		 *
		 * @return 由 high 的 8 字节和 low 的 8 字节依次组成的标识，两部分均按最高有效字节优先排列。
		 */
		[[nodiscard]] static constexpr StableId FromWords(uint64_t high, uint64_t low) noexcept {
			StableId result;
			for (unsigned i = 0; i < 8; ++i) {
				result.bytes[i] = static_cast<uint8_t>(high >> (56 - i * 8));
				result.bytes[i + 8] = static_cast<uint8_t>(low >> (56 - i * 8));
			}
			return result;
		}

		/**
		 * @brief 转换为字符串。
		 *
		 * @return 以小写十六进制及 8-4-4-4-12 分组表示的稳定标识文本。
		 */
		[[nodiscard]] std::string ToString() const {
			constexpr char hex[] = "0123456789abcdef";
			std::string text;
			text.reserve(36);
			for (size_t i = 0; i < bytes.size(); ++i) {
				if (i == 4 || i == 6 || i == 8 || i == 10)
					text.push_back('-');
				text.push_back(hex[bytes[i] >> 4]);
				text.push_back(hex[bytes[i] & 15]);
			}
			return text;
		}

		/**
		 * @brief 尝试从输入构造目标值，返回解析是否成功。
		 *
		 * @param[in] text 待解析为稳定标识的 UUID 字符串。
		 * @param[out] output 接收稳定标识结果。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] static bool TryParse(std::string_view text, StableId& output) noexcept {
			if (text.size() != 36)
				return false;
			auto nibble = [](char ch) -> int {
				if (ch >= '0' && ch <= '9')
					return ch - '0';
				if (ch >= 'a' && ch <= 'f')
					return ch - 'a' + 10;
				if (ch >= 'A' && ch <= 'F')
					return ch - 'A' + 10;
				return -1;
			};
			StableId candidate;
			size_t cursor = 0;
			for (size_t i = 0; i < candidate.bytes.size(); ++i) {
				if (i == 4 || i == 6 || i == 8 || i == 10) {
					if (text[cursor++] != '-')
						return false;
				}
				const int high = nibble(text[cursor++]);
				const int low = nibble(text[cursor++]);
				if (high < 0 || low < 0)
					return false;
				candidate.bytes[i] = static_cast<uint8_t>((high << 4) | low);
			}
			if (!candidate.HasValue())
				return false;
			output = candidate;
			return true;
		}
		constexpr auto operator<=>(const StableId&) const = default;
	};

}
