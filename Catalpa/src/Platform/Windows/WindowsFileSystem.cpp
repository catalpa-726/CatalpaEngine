#include "catpch.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"

#if defined(CATALPA_PLATFORM_WINDOW)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Catalpa::FileSystemDetail {
#if defined(CATALPA_PLATFORM_WINDOW)
    namespace {
        // Own native handles only inside the platform adapter, never in public IO values.
        class NativeFile {
        public:
            explicit NativeFile(HANDLE handle = INVALID_HANDLE_VALUE) : m_Handle(handle) {}
            ~NativeFile() { if (Valid()) CloseHandle(m_Handle); }
            NativeFile(const NativeFile&) = delete;
            NativeFile& operator=(const NativeFile&) = delete;
            bool Valid() const { return m_Handle != INVALID_HANDLE_VALUE; }
            HANDLE Get() const { return m_Handle; }
        private:
            HANDLE m_Handle;
        };

        bool Fail(std::string& error, std::string_view operation, DWORD code = GetLastError())
        {
            error = std::string(operation) + " (Windows error " + std::to_string(code) + ")";
            return false;
        }

        bool Missing(DWORD code) { return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND; }

        bool FinalPath(HANDLE handle, std::filesystem::path& output, std::string& error)
        {
            std::vector<wchar_t> buffer(32768);
            const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
                static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (length == 0) return Fail(error, "Cannot resolve final file path");
            if (length >= buffer.size()) { error = "Final file path exceeds the supported length"; return false; }
            std::wstring text(buffer.data(), length);
            // Retain the separator on a drive root (\\?\C:\), remove only redundant directory suffixes.
            while (text.size() > 7 && text.back() == L'\\') text.pop_back();
            output = std::filesystem::path(std::move(text));
            return true;
        }

        bool Same(std::wstring_view a, std::wstring_view b)
        {
            return a.size() == b.size() && a.size() <= std::numeric_limits<int>::max() &&
                CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                    static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
        }

        bool Within(const std::filesystem::path& candidate, const std::filesystem::path& root)
        {
            const auto& file = candidate.native();
            auto prefix = root.native();
            if (prefix.empty() || prefix.back() != L'\\') prefix += L'\\';
            // Final handle paths authorize access, so never case-fold them: Windows also supports
            // case-sensitive directories where differently cased siblings are distinct roots.
            return file.size() > prefix.size() && std::wstring_view(file).starts_with(prefix);
        }

        bool Utf8ToWide(std::string_view text, std::wstring& output, std::string& error)
        {
            if (text.size() > std::numeric_limits<int>::max()) { error = "UTF-8 path is too long"; return false; }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0) return Fail(error, "Cannot decode UTF-8 path");
            std::wstring result(static_cast<size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), result.data(), count) != count) return Fail(error, "Cannot decode UTF-8 path");
            output = std::move(result);
            return true;
        }

        bool ValidAbsolute(const std::filesystem::path& path, std::string& error)
        {
            const auto& text = path.native();
            if (!path.is_absolute() || text.empty() || text.size() > 32760 ||
                text.starts_with(L"\\\\?\\") || text.starts_with(L"\\\\.\\")) {
                error = "Physical path must be an ordinary absolute drive or UNC path"; return false;
            }
            for (size_t i = 0; i < text.size(); ++i) {
                const wchar_t c = text[i];
                if (c < 32 || c == 127 || c == L'<' || c == L'>' || c == L'"' || c == L'|' ||
                    c == L'?' || c == L'*' || (c == L':' && i != 1)) {
                    error = "Physical path contains a stream, device or invalid character"; return false;
                }
            }
            const auto relative = path.relative_path().generic_u8string();
            if (!relative.empty()) {
                VirtualPath checked;
                if (!VirtualPath::Parse("physical:/" + std::string(reinterpret_cast<const char*>(relative.data()), relative.size()), checked, error)) return false;
            }
            return true;
        }

        bool DirectoryHandle(HANDLE handle, std::string& error)
        {
            BY_HANDLE_FILE_INFORMATION information{};
            if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &information))
                return Fail(error, "Cannot inspect directory handle");
            if (!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { error = "Physical root is not a directory"; return false; }
            return true;
        }

        // Exact spelling excludes Windows case and short-name aliases; each opened component is
        // checked after following reparse points, so even an intermediate junction cannot escape.
        bool ResolveSpelling(const std::filesystem::path& root, const std::wstring& relative,
            std::filesystem::path& output, bool& missing, std::string& error)
        {
            std::filesystem::path current = root;
            for (size_t begin = 0; begin < relative.size();) {
                const size_t slash = relative.find(L'/', begin);
                const auto segment = relative.substr(begin, slash == std::wstring::npos ? slash : slash - begin);
                current /= segment;
                WIN32_FIND_DATAW entry{};
                const HANDLE search = FindFirstFileW(current.c_str(), &entry);
                if (search == INVALID_HANDLE_VALUE) {
                    const DWORD code = GetLastError(); missing = Missing(code);
                    return Fail(error, "Cannot locate virtual path component", code);
                }
                FindClose(search);
                if (segment != entry.cFileName) { error = "Virtual path case or short-name alias differs from the actual file name"; return false; }
                NativeFile component(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                if (!component.Valid()) {
                    const DWORD code = GetLastError();
                    // A visible reparse point with an unavailable target is an error, not an overlay miss.
                    missing = Missing(code) && !(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
                    return Fail(error, "Cannot open virtual path component", code);
                }
                std::filesystem::path final;
                if (!FinalPath(component.Get(), final, error)) return false;
                if (!Within(final, root)) { error = "Virtual path escapes its mounted root"; return false; }
                if (slash != std::wstring::npos && !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    error = "An intermediate virtual path component is not a directory"; return false;
                }
                if (slash == std::wstring::npos) break;
                begin = slash + 1;
            }
            output = std::move(current);
            return true;
        }

        // Failure cleanup uses the owned handle, never an attacker-replaceable path or the target.
        class TemporaryFile {
        public:
            TemporaryFile(HANDLE handle, std::string& error) : m_Handle(handle), m_Error(error) {}
            ~TemporaryFile() {
                if (!m_Committed) {
                    FILE_DISPOSITION_INFO disposition{TRUE};
                    if (!SetFileInformationByHandle(m_Handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
                        const DWORD code = GetLastError();
                        // Report a cleanup failure without throwing from a destructor during unwinding.
                        try { m_Error += "; temporary cleanup failed (Windows error " + std::to_string(code) + ")"; }
                        catch (...) {}
                    }
                }
                CloseHandle(m_Handle);
            }
            HANDLE Get() const { return m_Handle; }
            void Commit() { m_Committed = true; }
            TemporaryFile(const TemporaryFile&) = delete;
            TemporaryFile& operator=(const TemporaryFile&) = delete;
        private:
            HANDLE m_Handle;
            std::string& m_Error;
            bool m_Committed = false;
        };
    }
#endif

    bool EqualRoots(const std::filesystem::path& a, const std::filesystem::path& b)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        return Same(a.native(), b.native());
#else
        return a == b;
#endif
    }

    bool CanonicalizeRoot(const std::filesystem::path& root, std::filesystem::path& output, std::string& error)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        if (!ValidAbsolute(root, error)) return false;
        NativeFile directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!directory.Valid()) return Fail(error, "Cannot open physical root");
        if (!DirectoryHandle(directory.Get(), error)) return false;
        if (!FinalPath(directory.Get(), output, error)) return false;
        error.clear();
        return true;
#else
        (void)root; (void)output; error = "Filesystem backend is unavailable on this platform"; return false;
#endif
    }

    bool ReadFromRoot(const std::filesystem::path& root, std::string_view relative, size_t maxBytes,
        std::vector<std::byte>& output, bool& missing, std::string& error)
    {
        missing = false;
#if defined(CATALPA_PLATFORM_WINDOW)
        // Keep the configured root in place during resolution and reading.
        NativeFile directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!directory.Valid()) return Fail(error, "Mounted root is unavailable");
        std::filesystem::path actualRoot;
        if (!DirectoryHandle(directory.Get(), error) || !FinalPath(directory.Get(), actualRoot, error)) return false;
        if (root.native() != actualRoot.native()) { error = "Mounted root was redirected after configuration"; return false; }
        std::wstring wide;
        if (!Utf8ToWide(relative, wide, error)) return false;
        std::filesystem::path resolved;
        if (!ResolveSpelling(root, wide, resolved, missing, error)) return false;
        NativeFile file(CreateFileW(resolved.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!file.Valid()) {
            // The path existed during resolution; a concurrent mutation is not a clean overlay miss.
            return Fail(error, "Cannot open resolved file for reading");
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileType(file.Get()) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.Get(), &information))
            return Fail(error, "Cannot inspect readable file");
        if (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { error = "Virtual path does not refer to an ordinary file"; return false; }
        std::filesystem::path final;
        if (!FinalPath(file.Get(), final, error)) return false;
        if (!Within(final, root)) { error = "Opened file escapes its mounted root"; return false; }
        const uint64_t length = (static_cast<uint64_t>(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
        if (length > maxBytes || length > std::numeric_limits<size_t>::max()) { error = "File exceeds the requested read limit"; return false; }
        std::vector<std::byte> bytes(static_cast<size_t>(length));
        for (size_t offset = 0; offset < bytes.size();) {
            const DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD received = 0;
            if (!::ReadFile(file.Get(), bytes.data() + offset, count, &received, nullptr)) return Fail(error, "Cannot read file bytes");
            if (received == 0) { error = "File ended before its reported size"; return false; }
            offset += received;
        }
        output = std::move(bytes);
        error.clear();
        return true;
#else
        (void)root; (void)relative; (void)maxBytes; (void)output;
        error = "Filesystem backend is unavailable on this platform"; return false;
#endif
    }

    bool WriteAtomic(const std::filesystem::path& target, std::span<const std::byte> bytes, std::string& error)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        if (!ValidAbsolute(target, error)) return false;
        if (target.filename().empty()) { error = "Save target has no file name"; return false; }
        std::filesystem::path parent;
        if (!CanonicalizeRoot(target.parent_path(), parent, error)) return false;
        NativeFile parentHandle(CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!parentHandle.Valid()) return Fail(error, "Cannot pin save directory");
        std::filesystem::path pinnedParent;
        if (!DirectoryHandle(parentHandle.Get(), error) || !FinalPath(parentHandle.Get(), pinnedParent, error)) return false;
        if (parent.native() != pinnedParent.native()) { error = "Save directory was redirected"; return false; }
        const auto destination = parent / target.filename();
        const DWORD attributes = GetFileAttributesW(destination.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
                error = "Save target must not be a directory or reparse point"; return false;
            }
            WIN32_FIND_DATAW entry{};
            const HANDLE search = FindFirstFileW(destination.c_str(), &entry);
            if (search == INVALID_HANDLE_VALUE) return Fail(error, "Cannot inspect save target name");
            FindClose(search);
            if (target.filename().native() != entry.cFileName) { error = "Save target uses a case or short-name alias"; return false; }
        } else {
            const DWORD code = GetLastError();
            if (!Missing(code)) return Fail(error, "Cannot inspect save target", code);
        }
        HANDLE created = INVALID_HANDLE_VALUE;
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            std::array<std::byte, 16> random{};
            if (!FillRandomBytes(random, error)) return false;
            std::wstring name = L".catalpa-write-";
            constexpr wchar_t hex[] = L"0123456789abcdef";
            for (std::byte byte : random) {
                const auto value = std::to_integer<unsigned>(byte);
                name += hex[value >> 4]; name += hex[value & 15];
            }
            name += L".tmp";
            created = CreateFileW((parent / name).c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE,
                0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (created != INVALID_HANDLE_VALUE) break;
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) return Fail(error, "Cannot create exclusive save temporary", code);
        }
        if (created == INVALID_HANDLE_VALUE) { error = "Cannot reserve a unique save temporary"; return false; }
        TemporaryFile temporary(created, error);
        std::filesystem::path tempFinal, parentNow;
        if (!FinalPath(temporary.Get(), tempFinal, error) || !FinalPath(parentHandle.Get(), parentNow, error)) return false;
        if (tempFinal.parent_path().native() != parentNow.native()) { error = "Save temporary is outside the pinned parent"; return false; }
        for (size_t offset = 0; offset < bytes.size();) {
            const DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD written = 0;
            if (!WriteFile(temporary.Get(), bytes.data() + offset, count, &written, nullptr)) return Fail(error, "Cannot write save temporary");
            if (written == 0) { error = "Save temporary write made no progress"; return false; }
            offset += written;
        }
        if (!FlushFileBuffers(temporary.Get())) return Fail(error, "Cannot flush save temporary");
        // 目录句柄保持打开并在提交前再次核对；作者调用方仍须保持祖先目录拓扑稳定。
        if (!FinalPath(parentHandle.Get(), parentNow, error) || !FinalPath(temporary.Get(), tempFinal, error)) return false;
        if (parentNow.native() != parent.native() || tempFinal.parent_path().native() != parentNow.native()) {
            error = "Save directory changed before commit"; return false;
        }
        // 使用Win32支持的完整绝对目标名，避免依赖非空RootDirectory的相对路径处理。
        // 父句柄继续用于固定/核对目录，但不作为此次FileRenameInfo的RootDirectory参数。
        const auto renamePath = (parentNow / target.filename()).native();
        if (renamePath.size() > (std::numeric_limits<DWORD>::max() - sizeof(FILE_RENAME_INFO)) / sizeof(wchar_t)) {
            error = "Save target exceeds the rename buffer limit"; return false;
        }
        const size_t nameBytes = renamePath.size() * sizeof(wchar_t);
        std::vector<std::byte> renameStorage(sizeof(FILE_RENAME_INFO) + nameBytes);
        auto* rename = ::new (static_cast<void*>(renameStorage.data())) FILE_RENAME_INFO{};
        rename->ReplaceIfExists = TRUE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(nameBytes);
        std::memcpy(rename->FileName, renamePath.c_str(), nameBytes + sizeof(wchar_t));
        if (!SetFileInformationByHandle(temporary.Get(), FileRenameInfo, rename, static_cast<DWORD>(renameStorage.size())))
            return Fail(error, "Cannot commit save temporary");
        temporary.Commit();
        error.clear();
        return true;
#else
        (void)target; (void)bytes; error = "Atomic save is unavailable on this platform"; return false;
#endif
    }
}
