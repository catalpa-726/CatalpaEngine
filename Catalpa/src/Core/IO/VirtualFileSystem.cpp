#include "catpch.h"
#include "Core/IO/VirtualFileSystem.h"

namespace Catalpa::FileSystemDetail {
    bool CanonicalizeRoot(const std::filesystem::path&, std::filesystem::path&, std::string&);
    bool EqualRoots(const std::filesystem::path&, const std::filesystem::path&);
    bool ReadFromRoot(const std::filesystem::path&, std::string_view, size_t,
        std::vector<std::byte>&, bool&, std::string&);
}

namespace Catalpa {
    namespace {
        // Validate scalar UTF-8, excluding overlong encodings, surrogates and out-of-range values.
        bool ValidUtf8(std::string_view value)
        {
            for (size_t i = 0; i < value.size();) {
                const auto first = static_cast<unsigned char>(value[i++]);
                if (first < 0x80) continue;
                unsigned remaining = 0;
                uint32_t scalar = 0, minimum = 0;
                if (first >= 0xC2 && first <= 0xDF) { remaining = 1; scalar = first & 0x1F; minimum = 0x80; }
                else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; scalar = first & 0x0F; minimum = 0x800; }
                else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; scalar = first & 0x07; minimum = 0x10000; }
                else return false;
                if (remaining > value.size() - i) return false;
                while (remaining--) {
                    const auto next = static_cast<unsigned char>(value[i++]);
                    if ((next & 0xC0) != 0x80) return false;
                    scalar = (scalar << 6) | (next & 0x3F);
                }
                if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
            }
            return true;
        }

        bool ReservedDevice(std::string_view segment)
        {
            std::string stem(segment.substr(0, segment.find('.')));
            while (!stem.empty() && stem.back() == ' ') stem.pop_back();
            for (char& c : stem) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
                stem == "CLOCK$" || stem == "CONIN$" || stem == "CONOUT$") return true;
            if (stem.starts_with("COM") || stem.starts_with("LPT")) {
                const auto suffix = std::string_view(stem).substr(3);
                return (suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') ||
                    suffix == "\xC2\xB9" || suffix == "\xC2\xB2" || suffix == "\xC2\xB3";
            }
            return false;
        }
    }

    bool VirtualPath::Parse(std::string_view text, VirtualPath& output, std::string& error)
    {
        try {
            const size_t separator = text.find(":/");
            if (text.size() > 32760 || separator == std::string_view::npos || separator == 0 || separator > 63) {
                error = "Virtual path must be mount:/relative and within the supported length"; return false;
            }
            const auto mount = text.substr(0, separator);
            if (mount.front() < 'a' || mount.front() > 'z') { error = "Mount must begin with a lowercase ASCII letter"; return false; }
            for (char c : mount) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                    error = "Mount contains a noncanonical character"; return false;
                }
            }
            const auto relative = text.substr(separator + 2);
            if (relative.empty() || !ValidUtf8(relative)) { error = "Relative path must be nonempty valid UTF-8"; return false; }
            for (unsigned char c : relative) {
                if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '<' || c == '>' ||
                    c == '"' || c == '|' || c == '?' || c == '*') {
                    error = "Relative path contains a forbidden Windows path character"; return false;
                }
            }
            for (size_t begin = 0; begin <= relative.size();) {
                const size_t slash = relative.find('/', begin);
                const auto segment = relative.substr(begin, slash == std::string_view::npos ? slash : slash - begin);
                if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' ||
                    segment.back() == ' ' || ReservedDevice(segment)) {
                    error = "Relative path contains an empty, traversal, device or aliased component"; return false;
                }
                if (slash == std::string_view::npos) break;
                begin = slash + 1;
            }
            VirtualPath parsed;
            parsed.m_Text = text;
            parsed.m_Mount = mount;
            parsed.m_Relative = relative;
            output = std::move(parsed);
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool VirtualFileSystem::MountReadOnly(std::string_view mount, const std::filesystem::path& absoluteRoot,
        int priority, std::string& error)
    {
        try {
            VirtualPath validated;
            if (!VirtualPath::Parse(std::string(mount) + ":/__mount_check__", validated, error)) return false;
            if (validated.MountName() != mount) { error = "Mount argument must contain only its name"; return false; }
            std::filesystem::path canonical;
            if (!FileSystemDetail::CanonicalizeRoot(absoluteRoot, canonical, error)) return false;
            for (const auto& item : m_Mounts) {
                if (item.name == mount && (item.priority == priority || FileSystemDetail::EqualRoots(item.root, canonical))) {
                    error = "Mount priority or physical root is already registered for this name"; return false;
                }
            }
            m_Mounts.push_back({std::string(mount), std::move(canonical), priority});
            std::sort(m_Mounts.begin(), m_Mounts.end(), [](const Mount& a, const Mount& b) { return a.priority > b.priority; });
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool VirtualFileSystem::ReadFile(const VirtualPath& path, size_t maxBytes,
        std::vector<std::byte>& output, std::string& error) const
    {
        try {
            if (!path.HasValue()) { error = "Virtual path is invalid"; return false; }
            bool foundMount = false;
            for (const auto& mount : m_Mounts) {
                if (mount.name != path.MountName()) continue;
                foundMount = true;
                bool missing = false;
                if (FileSystemDetail::ReadFromRoot(mount.root, path.RelativePath(), maxBytes, output, missing, error)) return true;
                if (!missing) return false;
            }
            error = foundMount ? "File does not exist in any configured root" : "Virtual mount is not configured";
            return false;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
