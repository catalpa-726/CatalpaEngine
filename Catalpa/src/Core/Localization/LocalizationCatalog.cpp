#include "catpch.h"
#include "Core/Localization/LocalizationCatalog.h"
#include "Core/Serialization/JsonArchive.h"

namespace Catalpa {
    bool LocalizationCatalog::Load(std::string_view bytes, std::string_view language, std::string& error) {
        try {
            if (language != "en-US" && language != "zh-CN") {
                error = "Unsupported catalog language";
                return false;
            }
            ArchiveLimits limits;
            limits.maxBytes = 512 * 1024;
            limits.maxDepth = 4;
            limits.maxNodes = 16384;
            limits.maxStringBytes = 2048;
            limits.maxCollectionEntries = 4096;
            std::unique_ptr<ArchiveValue> document;
            ArchiveError diagnostic;
            if (!JsonArchive::Parse(bytes, document, diagnostic, limits)) {
                error = diagnostic.ToString();
                return false;
            }
            const auto* root = std::get_if<ArchiveValue::Object>(&document->data);
            if (!root || root->size() != 2) {
                error = "Catalog requires exactly language and strings";
                return false;
            }
            const auto* id = document->Find("language");
            const auto* values = document->Find("strings");
            const auto* idText = id ? std::get_if<std::string>(&id->data) : nullptr;
            const auto* entries = values ? std::get_if<ArchiveValue::Object>(&values->data) : nullptr;
            if (!idText || *idText != language || !entries) {
                error = "Catalog language/strings mismatch";
                return false;
            }
            std::map<std::string, std::string, std::less<>> candidate;
            for (const auto& [key, value] : *entries) {
                const auto* text = std::get_if<std::string>(&value.data);
                if (key.empty() || key.size() > 128 ||
                    std::any_of(key.begin(), key.end(),
                                [](unsigned char c) {
                                    return !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                                             c == '_' || c == '-');
                                }) ||
                    !text || text->empty() || text->find("##") != std::string::npos ||
                    std::any_of(text->begin(), text->end(),
                                [](unsigned char c) { return c < 32 || c == 127; })) {
                    error = "Invalid catalog text at key: " + key;
                    return false;
                }
                candidate.emplace(key, *text);
            }
            std::string nextLanguage(language);
            m_Strings.swap(candidate);
            m_Language.swap(nextLanguage);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    const char* LocalizationCatalog::Text(const char* key, const char* fallback) const noexcept {
        const auto found = m_Strings.find(key);
        return found == m_Strings.end() ? fallback : found->second.c_str();
    }
} // namespace Catalpa
