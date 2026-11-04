#pragma once

#include "catpch.h"

namespace Catalpa {

    // 稳定的领域注册键；不持有具体资产类型，不把 typeid 或 DLL 地址写入持久化数据。
    class AssetType {
    public:
        [[nodiscard]] bool HasValue() const noexcept { return !m_Name.empty(); }
        [[nodiscard]] const std::string& ToString() const noexcept { return m_Name; }

        // 1..128 字节的小写 ASCII 键：首字符 a-z，其余允许 a-z、0-9、点、下划线和短横线。
        // 不转换大小写；失败保留输出，领域名称由 Runtime/Tools 注册时选择。
        [[nodiscard]] static bool TryParse(std::string_view text, AssetType& output) {
            if (text.empty() || text.size() > 128 || text.front() < 'a' || text.front() > 'z') return false;
            for (char c : text) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
            }
            try {
                std::string candidate(text);
                output.m_Name.swap(candidate);
                return true;
            }
            catch (const std::bad_alloc&) { return false; }
            catch (const std::length_error&) { return false; }
        }

        auto operator<=>(const AssetType&) const = default;

    private:
        std::string m_Name;
    };

}
