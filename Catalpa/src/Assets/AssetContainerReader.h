#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetContainerFormat.h"
namespace Catalpa {
    class CATALPA_API AssetContainerReader {
    public:
        static bool Read(std::span<const std::byte>, AssetContainer&, std::string& error,
            size_t maxPayloadBytes = 64 * 1024 * 1024);
    };
    // Shared in-memory encoder for Tools and tests; only the caller's explicit output policy writes files.
    CATALPA_API bool WriteAssetContainer(const AssetContainer&, std::vector<std::byte>& output,
        std::string& error, size_t maxBytes = 64 * 1024 * 1024);
    CATALPA_API bool ComputeAssetContentHash(std::span<const std::byte>, std::string& output, std::string& error);
}
