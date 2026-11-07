#pragma once
#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    // 领域无关的 UTF-8 文本表。失败保留旧语言；调用方单独维护稳定控件身份。
    class CATALPA_API LocalizationCatalog final {
      public:
        bool Load(std::string_view json, std::string_view expectedLanguage, std::string& error);
        const char* Text(const char* key, const char* fallback) const noexcept;
        const std::string& Language() const noexcept {
            return m_Language;
        }

      private:
        std::string m_Language = "en-US";
        std::map<std::string, std::string, std::less<>> m_Strings;
    };
} // namespace Catalpa
