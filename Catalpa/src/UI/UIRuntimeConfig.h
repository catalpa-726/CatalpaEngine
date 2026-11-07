#pragma once
#include "Core/Base.h"
#include <cstdint>
#include <string>
#include <string_view>
namespace Catalpa {
    struct UIRuntimeConfig {
        float density = 1;
        uint32_t maxDocuments = 16;
    };
    CATALPA_API bool ReadUIRuntimeConfig(std::string_view, UIRuntimeConfig&, std::string& error);
} // namespace Catalpa
