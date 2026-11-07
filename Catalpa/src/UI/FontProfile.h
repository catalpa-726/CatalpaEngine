#pragma once
#include "Core/Base.h"
#include <cstdint>
#include <string>
#include <string_view>
namespace Catalpa {
    struct FontProfile {
        std::string previewText;
        uint32_t previewPixelSize = 24, previewWidth = 640, previewHeight = 256;
    };
    CATALPA_API bool ReadFontProfile(std::string_view, FontProfile&, std::string& error);
} // namespace Catalpa
