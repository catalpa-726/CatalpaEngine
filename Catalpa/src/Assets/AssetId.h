#pragma once

#include "catpch.h"

#include "Core/Id/StableId.h"

namespace Catalpa {

	/* 持久化资产身份；不与场景作者 ID、路径、内容版本或运行期槽位代次隐式互转。 */
	struct AssetId {
		StableId value;

		constexpr AssetId() = default;
		explicit constexpr AssetId(StableId stable) noexcept : value(stable) {}
		[[nodiscard]] constexpr bool HasValue() const noexcept { return value.HasValue(); }
		[[nodiscard]] static constexpr AssetId FromWords(uint64_t high, uint64_t low) noexcept {
			return AssetId{StableId::FromWords(high, low)};
		}

		/**
		 * @brief 尝试从输入构造目标值，返回解析是否成功。
		 *
		 * @param[in] text 待解析为稳定标识的 UUID 字符串。
		 * @param[out] output 接收资产标识结果。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] static bool TryParse(std::string_view text, AssetId& output) {
			StableId candidate;
			if (!StableId::TryParse(text, candidate))
				return false;
			output = AssetId{candidate};
			return true;
		}
		[[nodiscard]] std::string ToString() const { return value.ToString(); }
		constexpr auto operator<=>(const AssetId&) const = default;
	};

}
