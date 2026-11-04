#include "catpch.h"
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

namespace Catalpa {
    bool GetExecutableDirectory(std::filesystem::path& output, std::string& error)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        try {
            for (size_t capacity = 256; capacity <= 32768; capacity *= 2) {
                std::vector<wchar_t> buffer(capacity);
                const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (size == 0) { error = "Cannot query executable path"; return false; }
                if (size >= buffer.size()) continue;
                auto directory = std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
                if (directory.empty()) { error = "Executable directory is empty"; return false; }
                output = std::move(directory);
                error.clear();
                return true;
            }
            error = "Executable path exceeds the supported size";
        } catch (const std::exception& exception) { error = exception.what(); }
#else
        error = "Executable directory is unavailable on this platform";
#endif
        return false;
    }
}
