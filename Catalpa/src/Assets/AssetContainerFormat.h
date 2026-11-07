#pragma once
#include "catpch.h"
#include "Assets/AssetId.h"
#include "Assets/AssetType.h"
namespace Catalpa {
    struct AssetDependency {
        AssetId id;
        uint64_t version = 0;
        // SHA-256 of the exact dependency container bytes, not a path or an invented build ID.
        std::string contentHash;
    };
    // Format 1 is LE with one raw payload block; format 2 wraps it in a bounded zstd frame.
    // Native structs are never dumped to disk.
    // Header/identity/type/dependency bounds precede block offset/length and a SHA-256 payload digest.
    struct AssetContainer {
        AssetId id;
        AssetType type;
        uint64_t version = 0;
        uint32_t payloadVersion = 1;
        std::vector<AssetDependency> dependencies;
        std::vector<std::byte> payload;
    };
}
