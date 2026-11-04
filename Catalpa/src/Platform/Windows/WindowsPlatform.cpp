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
#include <bcrypt.h>
#include <shellapi.h>
#endif

namespace Catalpa {
    bool ComputeSha256(std::span<const std::byte> input, std::array<std::byte, 32>& output, std::string& error)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        struct HashOwner {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            ~HashOwner() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
        } owner;
        auto status = BCryptOpenAlgorithmProvider(&owner.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status >= 0) status = BCryptCreateHash(owner.algorithm, &owner.hash, nullptr, 0, nullptr, 0, 0);
        size_t offset = 0;
        while (status >= 0 && offset < input.size()) {
            const auto count = static_cast<ULONG>(std::min<size_t>(input.size() - offset, 1024 * 1024));
            status = BCryptHashData(owner.hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data() + offset)), count, 0);
            offset += count;
        }
        std::array<std::byte, 32> candidate{};
        if (status >= 0) status = BCryptFinishHash(owner.hash, reinterpret_cast<PUCHAR>(candidate.data()), static_cast<ULONG>(candidate.size()), 0);
        if (status < 0) { error = "SHA-256 failed (NTSTATUS " + std::to_string(status) + ")"; return false; }
        output = candidate;
        error.clear();
        return true;
#else
        error = "SHA-256 platform provider is unavailable";
        return false;
#endif
    }

    bool IsPathWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
    {
        if (!candidate.is_absolute() || !root.is_absolute()) return false;
        for (const auto& part : candidate) if (part == "." || part == "..") return false;
        auto current = candidate.begin();
        for (const auto& expected : root) {
            if (expected.empty()) continue;
            if (expected == "." || expected == "..") return false;
            if (current == candidate.end()) return false;
#if defined(CATALPA_PLATFORM_WINDOW)
            const auto& a = current->native();
            const auto& b = expected.native();
            if (a.size() > static_cast<size_t>(std::numeric_limits<int>::max())
                || b.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
            if (CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) != CSTR_EQUAL) return false;
#else
            if (*current != expected) return false;
#endif
            ++current;
        }
        return true;
    }

    bool FillRandomBytes(std::span<std::byte> output, std::string& error)
    {
        if (output.size() > 1024 * 1024) { error = "Random byte request exceeds 1 MiB"; return false; }
#if defined(CATALPA_PLATFORM_WINDOW)
        try {
            std::vector<std::byte> candidate(output.size());
            if (!candidate.empty()) {
                const auto status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(candidate.data()),
                    static_cast<ULONG>(candidate.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
                if (status < 0) { error = "System random source failed (NTSTATUS " + std::to_string(status) + ")"; return false; }
            }
            std::copy(candidate.begin(), candidate.end(), output.begin());
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); }
#else
        error = "System random source is unavailable on this platform";
#endif
        return false;
    }

    bool GetProcessArguments(std::vector<std::string>& output, std::string& error)
    {
#if defined(CATALPA_PLATFORM_WINDOW)
        int count = 0;
        auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) { error = "Cannot read Unicode process arguments"; return false; }
        // CommandLineToArgvW分配一个连续块，所有失败/异常路径都用LocalFree回收。
        struct ArgumentsOwner { LPWSTR* data; ~ArgumentsOwner() { LocalFree(data); } } owner{ arguments };
        try {
            std::vector<std::string> candidate;
            candidate.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const auto length = static_cast<int>(std::wcslen(arguments[i]));
                if (length == 0) { candidate.emplace_back(); continue; }
                const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, arguments[i], length, nullptr, 0, nullptr, nullptr);
                if (bytes == 0) { error = "Process argument is not valid Unicode"; return false; }
                std::string text(static_cast<size_t>(bytes), '\0');
                if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, arguments[i], length, text.data(), bytes, nullptr, nullptr) != bytes) {
                    error = "Cannot convert process argument to UTF-8"; return false;
                }
                candidate.push_back(std::move(text));
            }
            output.swap(candidate);
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); }
#else
        error = "Process arguments are unavailable on this platform";
#endif
        return false;
    }

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
