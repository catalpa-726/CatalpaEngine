#pragma once

#include "Core/Id/StableId.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa {

	/*
	 * 场景作者身份，不与资产ID或可复用的运行期EntityId混用。
	 * 新建实体时生成；从文档恢复时沿用原身份。
	 */
	struct PersistentEntityId {
		StableId value;
		[[nodiscard]] constexpr bool HasValue() const noexcept { return value.HasValue(); }
		[[nodiscard]] static constexpr PersistentEntityId FromWords(uint64_t high, uint64_t low) noexcept {
			return {StableId::FromWords(high, low)};
		}
		[[nodiscard]] std::string ToString() const { return value.ToString(); }
		/**
		 * @brief 尝试从输入构造目标值，返回解析是否成功。
		 *
		 * @param[in] text 待解析为稳定标识的 UUID 字符串。
		 * @param[out] output 接收持久实体标识结果。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] static bool TryParse(std::string_view text, PersistentEntityId& output) noexcept {
			StableId id;
			if (!StableId::TryParse(text, id))
				return false;
			output = PersistentEntityId{id};
			return true;
		}
		constexpr auto operator<=>(const PersistentEntityId&) const = default;
	};

	/**
	 * @brief 生成持久实体标识。
	 *
	 * @param[out] output 接收持久实体标识结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] inline bool GeneratePersistentEntityId(PersistentEntityId& output, std::string& error) {
		StableId id;
		if (!GenerateStableId(id, error))
			return false;
		output = PersistentEntityId{id};
		return true;
	}

}
