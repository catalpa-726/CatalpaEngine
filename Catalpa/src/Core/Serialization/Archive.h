#pragma once

#include "catpch.h"

namespace Catalpa {

	/* 配置归档的独立值树；整数保持精确，不暴露 JSON 实现或领域对象。 */
	struct ArchiveValue {
		using Array = std::vector<ArchiveValue>;
		using Object = std::map<std::string, ArchiveValue, std::less<>>;
		using Data = std::variant<std::nullptr_t, bool, int64_t, uint64_t, double, std::string, Array, Object>;
		Data data = nullptr;

		/**
		 * @brief 在归档对象中查找指定字段；当前节点不是对象或字段缺失时返回空指针。
		 *
		 * @param[in] field 待查对象键。
		 *
		 * @return 当前对象所持字段值的指针；节点不是对象或字段不存在时返回 nullptr。
		 */
		[[nodiscard]] const ArchiveValue* Find(std::string_view field) const {
			const auto* object = std::get_if<Object>(&data);
			if (!object)
				return nullptr;
			const auto found = object->find(field);
			return found == object->end() ? nullptr : &found->second;
		}
		/**
		 * @brief 读取无符号整数。
		 *
		 * @param[out] output 接收64 位无符号整数结果。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] bool ReadUnsigned(uint64_t& output) const noexcept {
			if (const auto* number = std::get_if<uint64_t>(&data)) {
				output = *number;
				return true;
			}
			if (const auto* number = std::get_if<int64_t>(&data); number && *number >= 0) {
				output = static_cast<uint64_t>(*number);
				return true;
			}
			return false;
		}
	};

	/* 先按字节数、节点数、深度及集合大小检查所有输入，再按消费方的数据模式校验。 */
	struct ArchiveLimits {
		size_t maxBytes = 4 * 1024 * 1024;
		size_t maxDepth = 32;
		size_t maxNodes = 65536;
		size_t maxStringBytes = 256 * 1024;
		size_t maxCollectionEntries = 16384;
	};

	/* path 使用 JSON Pointer；根为空字符串，错误文本显示为 /。 */
	struct ArchiveError {
		std::string path;
		std::string message;
		[[nodiscard]] std::string ToString() const { return (path.empty() ? "/" : path) + ": " + message; }
	};

	/**
	 * @brief 对字段中的特殊字符进行 JSON Pointer 转义并追加到父路径。
	 *
	 * @param[in] parent 当前归档节点的路径。
	 * @param[in] field 子字段的原始键名。
	 *
	 * @return 父路径后追加一个经 JSON Pointer 转义的子字段所得到的路径。
	 */
	inline std::string ArchiveChildPath(std::string_view parent, std::string_view field) {
		std::string result(parent);
		result += '/';
		for (char value : field) {
			if (value == '~')
				result += "~0";
			else if (value == '/')
				result += "~1";
			else
				result += value;
		}
		return result;
	}
	/**
	 * @brief 检查必需的归档版本。
	 *
	 * @param[in] object 含 schemaVersion 字段的归档对象。
	 * @param[in] supported 支持状态。
	 * @param[in] error 失败时记录归档节点的路径和具体原因。
	 * @param[in] parent 父节点或父实体。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	inline bool RequireArchiveVersion(
	    const ArchiveValue& object, uint64_t supported, ArchiveError& error, std::string_view parent = {}) {
		const auto* field = object.Find("schemaVersion");
		uint64_t version = 0;
		if (!field || !field->ReadUnsigned(version) || version != supported) {
			error = {ArchiveChildPath(parent, "schemaVersion"),
			    "expected supported integer schema version " + std::to_string(supported)};
			return false;
		}
		return true;
	}
}
