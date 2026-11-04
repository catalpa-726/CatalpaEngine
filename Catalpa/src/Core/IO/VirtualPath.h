#pragma once

#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    // A locator, not an asset identity. UTF-8 mount:/relative paths keep the real file-name case.
    // Parse rejects aliases (dot segments, backslashes, DOS devices, streams and trailing dots/spaces).
    class CATALPA_API VirtualPath {
    public:
        VirtualPath() = default;
        VirtualPath(const VirtualPath&) = default;
        VirtualPath(VirtualPath&& other) noexcept { Swap(other); }
        // Publish all three related strings together, including when a copy allocation fails.
        VirtualPath& operator=(const VirtualPath& other) {
            if (this != &other) { VirtualPath candidate(other); Swap(candidate); }
            return *this;
        }
        VirtualPath& operator=(VirtualPath&& other) noexcept {
            if (this != &other) { VirtualPath candidate(std::move(other)); Swap(candidate); }
            return *this;
        }
        [[nodiscard]] static bool Parse(std::string_view text, VirtualPath& output, std::string& error);
        [[nodiscard]] bool HasValue() const noexcept { return !m_Text.empty(); }
        [[nodiscard]] const std::string& ToString() const noexcept { return m_Text; }
        [[nodiscard]] const std::string& MountName() const noexcept { return m_Mount; }
        [[nodiscard]] const std::string& RelativePath() const noexcept { return m_Relative; }
        bool operator==(const VirtualPath&) const = default;

    private:
        void Swap(VirtualPath& other) noexcept {
            m_Text.swap(other.m_Text);
            m_Mount.swap(other.m_Mount);
            m_Relative.swap(other.m_Relative);
        }
        std::string m_Text;
        std::string m_Mount;
        std::string m_Relative;
    };
}
