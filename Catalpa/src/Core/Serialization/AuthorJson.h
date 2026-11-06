#pragma once
#include "Core/Serialization/JsonArchive.h"
#include "Core/Math/TransformMath.h"
#include <nlohmann/json.hpp>

// 仅供各领域的实现文件包含。公共领域接口不暴露 JSON 库类型。
namespace Catalpa::AuthorJson {
    using Json = nlohmann::json;

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

    inline void Fields(const Json& value, std::initializer_list<std::string_view> allowed) {
        if (!value.is_object()) throw std::invalid_argument("Expected an author object");
        for (auto it = value.begin(); it != value.end(); ++it)
            if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
                throw std::invalid_argument("Unknown author field: " + it.key());
    }

    inline double Number(const Json& value) {
        if (!value.is_number()) throw std::invalid_argument("Expected a finite number");
        const auto number = value.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() || (number!=0 && static_cast<float>(number)==0))
            throw std::invalid_argument("Author number exceeds finite float range");
        return number;
    }

    inline uint64_t Unsigned(const Json& value) {
        if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() &&
                                         value.get<int64_t>() < 0))
            throw std::invalid_argument("Expected an unsigned integer");
        return value.get<uint64_t>();
    }

    template<class Id> Id Identity(const Json& value) {
        Id result;
        if (!value.is_string() || !Id::TryParse(value.get_ref<const std::string&>(), result))
            throw std::invalid_argument("Invalid persistent identity");
        return result;
    }

    inline Math::Vec3 Vec3(const Json& value) {
        if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected three coordinates");
        return {float(Number(value[0])), float(Number(value[1])), float(Number(value[2]))};
    }

    inline Json Array(const Math::Vec3& value) { return Json::array({value.x, value.y, value.z}); }
}
