#pragma once
#include "catpch.h"
#include "Assets/AssetLoader.h"
namespace Catalpa {
    // Domain codec combines the same runtime decoder with a checked payload encoder for Tools.
    // Stable type and payloadVersion select the protocol; cppType is only an in-process cast guard.
    struct AssetCodec {
        AssetLoader loader;
        uint32_t payloadVersion = 1;
        std::function<bool(const AssetLoadResult&, std::vector<std::byte>&, std::string&)> encode;
    };
}
