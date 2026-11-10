#pragma once
#include "Core/Localization/LocalizationCatalog.h"

namespace Catalpa::Editor {
    // A workspace owns its catalog. A nested editor or popup uses it only while that workspace draws.
    class EditorTextScope {
      public:
        explicit EditorTextScope(const LocalizationCatalog& catalog) : m_Previous(s_Current) { s_Current = &catalog; }
        ~EditorTextScope() { s_Current = m_Previous; }
        EditorTextScope(const EditorTextScope&) = delete;
        EditorTextScope& operator=(const EditorTextScope&) = delete;
        static const char* Get(const char* key, const char* fallback) noexcept {
            return s_Current ? s_Current->Text(key, fallback) : fallback;
        }
      private:
        inline static thread_local const LocalizationCatalog* s_Current = nullptr;
        const LocalizationCatalog* m_Previous;
    };

    inline const char* UiText(const char* key, const char* fallback) noexcept {
        return EditorTextScope::Get(key, fallback);
    }
    inline std::string UiLabel(const char* key, const char* fallback) {
        return std::string(UiText(key, fallback)) + "###" + key;
    }
    inline std::string UiChoices(std::initializer_list<const char*> options) {
        std::string list;
        for (const char* option : options) {
            list += option;
            list.push_back('\0');
        }
        return list;
    }
    // ImGui's formatted text is variadic. A translated message may change the words,
    // but must keep the same conversion specifiers and argument order.
    inline const char* UiFormat(const char* key, const char* fallback) noexcept {
        const char* translated = UiText(key, fallback);
        const auto next = [](const char*& cursor) -> std::string_view {
            cursor = std::strchr(cursor, '%');
            if (!cursor) return {};
            const char* start = cursor++;
            cursor += std::strspn(cursor, "-+ #0*.0123456789hljztL");
            if (*cursor) ++cursor;
            return {start, static_cast<size_t>(cursor - start)};
        };
        const char* a = fallback;
        const char* b = translated;
        for (;;) {
            const auto spec = next(a);
            if (spec != next(b)) return fallback;
            if (spec.empty()) return translated;
        }
    }
}
