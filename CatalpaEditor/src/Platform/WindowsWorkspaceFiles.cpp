#include "catpch.h"
#include "WorkspaceFiles.h"

#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Catalpa::Editor {

    namespace {

        // Windows 错误保留原始错误码，供现有 UI/日志入口报告，不创建另一套日志设施。
        std::string FileError(const char* operation, DWORD code)
        {
            return std::string(operation) + " failed (Win32 error " + std::to_string(code) + ").";
        }

        // 只管理当前打开的文件句柄；不在析构中重命名或删除用户文件。
        class ScopedFile {
        public:
            explicit ScopedFile(HANDLE handle) : m_Handle(handle) {}
            ~ScopedFile() { if (m_Handle != INVALID_HANDLE_VALUE) CloseHandle(m_Handle); }
            ScopedFile(const ScopedFile&) = delete;
            ScopedFile& operator=(const ScopedFile&) = delete;
            HANDLE Get() const { return m_Handle; }
            bool Close() { return CloseHandle(std::exchange(m_Handle, INVALID_HANDLE_VALUE)) != FALSE; }

        private:
            HANDLE m_Handle = INVALID_HANDLE_VALUE;
        };

    }

    bool GetWorkspacePaths(std::filesystem::path& executableDir, std::string& error)
    {
        error.clear();
        std::vector<wchar_t> buffer(512);
        while (buffer.size() <= 32768) {
            const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (count == 0) { error = FileError("GetModuleFileNameW", GetLastError()); return false; }
            if (count < buffer.size()) {
                const auto directory = std::filesystem::path(std::wstring(buffer.data(), count)).parent_path();
                if (directory.empty()) { error = "Executable directory is unavailable."; return false; }
                executableDir = directory;
                return true;
            }
            buffer.resize(buffer.size() * 2);
        }
        error = "Executable path exceeds the supported Windows path length.";
        return false;
    }

    bool ReadWorkspaceText(const std::filesystem::path& path, std::string& text, std::size_t maxBytes, std::string& error)
    {
        error.clear();
        // 共享读但不共享写/删除，保证本次有界读取期间文件不会被其他写者截断或替换。
        ScopedFile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (file.Get() == INVALID_HANDLE_VALUE) { error = FileError("Open workspace file", GetLastError()); return false; }
        LARGE_INTEGER size{};
        if (GetFileType(file.Get()) != FILE_TYPE_DISK || !GetFileSizeEx(file.Get(), &size)) {
            error = "Workspace input is not a readable regular file.";
            return false;
        }
        if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > maxBytes) {
            error = "Workspace file exceeds the allowed byte limit.";
            return false;
        }
        std::string loaded(static_cast<size_t>(size.QuadPart), '\0');
        size_t offset = 0;
        while (offset < loaded.size()) {
            const DWORD requested = static_cast<DWORD>(std::min<size_t>(loaded.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD count = 0;
            if (!ReadFile(file.Get(), loaded.data() + offset, requested, &count, nullptr)) {
                error = FileError("Read workspace file", GetLastError());
                return false;
            }
            if (count == 0) { error = "Workspace file ended before its reported size."; return false; }
            offset += count;
        }
        text = std::move(loaded);
        return true;
    }

    bool WriteWorkspaceTextAtomic(const std::filesystem::path& path, std::string_view text, std::string& error)
    {
        error.clear();
        if (path.empty() || !path.has_filename()) { error = "Workspace destination has no file name."; return false; }
        std::error_code directoryError;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError) { error = "Cannot create workspace directory: " + directoryError.message(); return false; }

        // CREATE_NEW 保证不覆盖遗留临时文件；同目录替换不允许跨卷复制退化。
        static std::atomic<uint64_t> sequence{ 0 };
        std::filesystem::path temporary;
        HANDLE native = INVALID_HANDLE_VALUE;
        for (unsigned int attempt = 0; attempt < 64; ++attempt) {
            temporary = path;
            temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
            native = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (native != INVALID_HANDLE_VALUE) break;
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) { error = FileError("Create workspace temporary file", code); return false; }
        }
        if (native == INVALID_HANDLE_VALUE) { error = "Cannot reserve a unique workspace temporary file."; return false; }
        ScopedFile file(native);
        const auto failWrite = [&](const char* operation, DWORD code) {
            file.Close();
            DeleteFileW(temporary.c_str());
            error = FileError(operation, code);
            return false;
        };

        size_t offset = 0;
        while (offset < text.size()) {
            const DWORD requested = static_cast<DWORD>(std::min<size_t>(text.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD count = 0;
            if (!WriteFile(file.Get(), text.data() + offset, requested, &count, nullptr)) return failWrite("Write workspace temporary file", GetLastError());
            if (count == 0) return failWrite("Write workspace temporary file", ERROR_WRITE_FAULT);
            offset += count;
        }
        if (!FlushFileBuffers(file.Get())) return failWrite("Flush workspace temporary file", GetLastError());
        if (!file.Close()) {
            const DWORD code = GetLastError();
            DeleteFileW(temporary.c_str());
            error = FileError("Close workspace temporary file", code);
            return false;
        }

        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                DeleteFileW(temporary.c_str());
                error = "Workspace destination is a directory.";
                return false;
            }
            auto backup = path;
            backup += L".bak";
            if (!CopyFileW(path.c_str(), backup.c_str(), FALSE)) {
                const DWORD code = GetLastError();
                DeleteFileW(temporary.c_str());
                error = FileError("Back up previous workspace settings", code);
                return false;
            }
        }
        else {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND) {
                DeleteFileW(temporary.c_str());
                error = FileError("Inspect workspace destination", code);
                return false;
            }
        }

        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD code = GetLastError();
            DeleteFileW(temporary.c_str());
            error = FileError("Replace workspace settings", code);
            return false;
        }
        return true;
    }

}
