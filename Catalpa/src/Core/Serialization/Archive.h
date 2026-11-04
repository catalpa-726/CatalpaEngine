#pragma once

#include "catpch.h"

namespace Catalpa {

    // 配置归档的独立值树；整数保持精确，不暴露 JSON 实现或领域对象。
    struct ArchiveValue {
        using Array = std::vector<ArchiveValue>;
        using Object = std::map<std::string, ArchiveValue, std::less<>>;
        using Data = std::variant<std::nullptr_t, bool, int64_t, uint64_t, double,
            std::string, Array, Object>;
        Data data = nullptr;

        [[nodiscard]] const ArchiveValue* Find(std::string_view field) const {
            const auto* object = std::get_if<Object>(&data);
            if (!object) return nullptr;
            const auto found = object->find(field);
            return found == object->end() ? nullptr : &found->second;
        }
        // 版本/计数只能取非负整数，不能把浮点数或负数静默转换。
        [[nodiscard]] bool ReadUnsigned(uint64_t& output) const noexcept {
            if (const auto* number = std::get_if<uint64_t>(&data)) { output = *number; return true; }
            if (const auto* number = std::get_if<int64_t>(&data); number && *number >= 0) {
                output = static_cast<uint64_t>(*number); return true;
            }
            return false;
        }
    };

    // 所有输入先受字节、节点、深度及集合大小限制，再按消费方 schema 检查。
    struct ArchiveLimits {
        size_t maxBytes = 4 * 1024 * 1024;
        size_t maxDepth = 32;
        size_t maxNodes = 65536;
        size_t maxStringBytes = 256 * 1024;
        size_t maxCollectionEntries = 16384;
    };

    // path 使用 JSON Pointer；根为空字符串，错误文本显示为 /。
    struct ArchiveError {
        std::string path;
        std::string message;
        [[nodiscard]] std::string ToString() const {
            return (path.empty() ? "/" : path) + ": " + message;
        }
    };

    // JSON Pointer 转义与版本诊断共用，不把字段名中的 / 或 ~ 误当路径分隔。
    inline std::string ArchiveChildPath(std::string_view parent, std::string_view field) {
        std::string result(parent);
        result += '/';
        for (char value : field) {
            if (value == '~') result += "~0";
            else if (value == '/') result += "~1";
            else result += value;
        }
        return result;
    }
    inline bool RequireArchiveVersion(const ArchiveValue& object, uint64_t supported,
        ArchiveError& error, std::string_view parent = {}) {
        const auto* field = object.Find("schemaVersion");
        uint64_t version = 0;
        if (!field || !field->ReadUnsigned(version) || version != supported) {
            error = { ArchiveChildPath(parent, "schemaVersion"),
                "expected supported integer schema version " + std::to_string(supported) };
            return false;
        }
        return true;
    }
}
