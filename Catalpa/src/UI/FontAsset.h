#pragma once
#include "catpch.h"
#include "Core/Base.h"
namespace Catalpa {
    struct FontAsset {
        std::string sourceUri, family;
        std::vector<std::byte> bytes;
        uint32_t glyphCount = 0;
    };
    CATALPA_API bool ValidateFontBytes(std::span<const std::byte>, std::string& family, uint32_t& glyphCount,
                                       std::string& error);
} // namespace Catalpa
