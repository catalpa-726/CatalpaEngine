#pragma once
#include "Core/Serialization/JsonArchive.h"
#include "Core/Math/TransformMath.h"

#include <nlohmann/json.hpp>

/* 仅供各领域的实现文件包含。公共领域接口不暴露 JSON 库类型。 */
namespace Catalpa::AuthorJson {
	using Json = nlohmann::json;

	/**
	 * @brief 在字节预算内检查并解析作者 JSON 文档。
	 *
	 * @param[in] text 完整的 JSON 文本。
	 * @param[in] maxBytes 此次读取或解析允许接受的最大字节数。
	 *
	 * @return 解析后的 JSON 文档。输入未通过归档格式或容量检查时抛出异常。
	 */
	inline Json Parse(std::string_view text, size_t maxBytes = 16 * 1024 * 1024) {
		std::unique_ptr<ArchiveValue> checked;
		ArchiveError error;
		ArchiveLimits limits;
		limits.maxBytes = maxBytes;
		limits.maxNodes = 1000000;
		limits.maxCollectionEntries = 65536;
		limits.maxStringBytes = maxBytes;
		if (!JsonArchive::Parse(text, checked, error, limits))
			throw std::invalid_argument(error.ToString());
		return Json::parse(text);
	}

	/**
	 * @brief 检查作者节点为对象并拒绝约定清单之外的字段。
	 *
	 * @param[in] value 待验的 JSON 节点。
	 * @param[in] allowed 此对象允许使用的字段名。
	 */
	inline void Fields(const Json& value, std::initializer_list<std::string_view> allowed) {
		if (!value.is_object())
			throw std::invalid_argument("Expected an author object");
		for (auto it = value.begin(); it != value.end(); ++it)
			if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
				throw std::invalid_argument("Unknown author field: " + it.key());
	}

	/**
	 * @brief 读取有限 JSON 数值并按目标浮点类型返回。
	 *
	 * @param[in] value 待解码的 JSON 值。
	 *
	 * @return 经有限性、溢出和下溢检查的 double 值，数值范围与单精度存储兼容。
	 */
	inline double Number(const Json& value) {
		if (!value.is_number())
			throw std::invalid_argument("Expected a finite number");
		const auto number = value.get<double>();
		if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() ||
		    (number != 0 && static_cast<float>(number) == 0))
			throw std::invalid_argument("Author number exceeds finite float range");
		return number;
	}

	/**
	 * @brief 读取非负整数，拒绝浮点数或负整数。
	 *
	 * @param[in] value 待解码的 JSON 值。
	 *
	 * @return 非负整数字段的 uint64_t 值；类型或符号不符合要求时抛出异常。
	 */
	inline uint64_t Unsigned(const Json& value) {
		if (!value.is_number_integer() ||
		    (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>() < 0))
			throw std::invalid_argument("Expected an unsigned integer");
		return value.get<uint64_t>();
	}

	/**
	 * @brief 把字符串解析为目标类型的有效标识。
	 *
	 * @param[in] value 待解码的 JSON 值。
	 *
	 * @return 从字符串解析的持久标识；字符串或标识无效时抛出异常。
	 */
	template <class Id> Id Identity(const Json& value) {
		Id result;
		if (!value.is_string() || !Id::TryParse(value.get_ref<const std::string&>(), result))
			throw std::invalid_argument("Invalid persistent identity");
		return result;
	}

	/**
	 * @brief 按 x、y、z 顺序读取三元素数值数组。
	 *
	 * @param[in] value 待解码的 JSON 值。
	 *
	 * @return 按输入数组顺序排列的 x、y、z 三个单精度分量。
	 */
	inline Math::Vec3 Vec3(const Json& value) {
		if (!value.is_array() || value.size() != 3)
			throw std::invalid_argument("Expected three coordinates");
		return {float(Number(value[0])), float(Number(value[1])), float(Number(value[2]))};
	}

	inline Json Array(const Math::Vec3& value) {
		return Json::array({value.x, value.y, value.z});
	}
}
