#pragma once

#include "catpch.h"

namespace Catalpa {

	/*
	 * 稳定的领域注册键；不持有具体资产类型，不把 typeid 或 DLL 地址写入持久化数据。
	 * 键为 1 到 128 字节的小写 ASCII，首字母为 a-z，其余允许字母、数字、点、下划线和短横线。
	 */
	class AssetType {
	public:
		[[nodiscard]] bool HasValue() const noexcept { return !m_Name.empty(); }
		[[nodiscard]] const std::string& ToString() const noexcept { return m_Name; }

		/**
		 * @brief 尝试从输入构造目标值，返回解析是否成功。
		 *
		 * @param[in] text 要解析为资产类型的文本名称。
		 * @param[out] output 接收 AssetType 结果。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] static bool TryParse(std::string_view text, AssetType& output) {
			if (text.empty() || text.size() > 128 || text.front() < 'a' || text.front() > 'z')
				return false;
			for (char c : text) {
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
					return false;
			}
			try {
				std::string candidate(text);
				output.m_Name.swap(candidate);
				return true;
			} catch (const std::bad_alloc&) {
				return false;
			} catch (const std::length_error&) {
				return false;
			}
		}

		auto operator<=>(const AssetType&) const = default;

	private:
		std::string m_Name;
	};

}
