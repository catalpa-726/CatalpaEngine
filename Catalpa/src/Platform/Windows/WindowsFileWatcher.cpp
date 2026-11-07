#include "catpch.h"
#include "Core/IO/FileWatcher.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Catalpa::FileWatcherDetail {
    bool Query(const std::filesystem::path& path, FileStamp& stamp, std::string& error) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
            const auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                stamp = {};
                error.clear();
                return true;
            }
            error = "Cannot inspect watched input (Windows error " + std::to_string(code) + ")";
            return false;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            error = "Watch input must be a file";
            return false;
        }
        stamp = {uint64_t(data.nFileSizeHigh) << 32 | data.nFileSizeLow,
                 uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime,
                 true};
        error.clear();
        return true;
    }
} // namespace Catalpa::FileWatcherDetail
