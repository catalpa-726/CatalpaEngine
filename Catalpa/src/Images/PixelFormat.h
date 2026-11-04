#pragma once

namespace Catalpa {

    enum class PixelFormat { Unknown = 0, RGBA8_UNorm = 1, RGBA8_SRGB = 2, BGRA8_UNorm = 3, BGRA8_SRGB = 4, D32_Float = 5 };

    struct PixelFormatInfo {
        unsigned int blockWidth = 0;
        unsigned int blockHeight = 0;
        unsigned int bytesPerBlock = 0;
        bool isSRGB = false;
        bool isDepth = false;
    };

    [[nodiscard]] constexpr PixelFormatInfo GetPixelFormatInfo(PixelFormat format) noexcept
    {
        switch (format) {
            case PixelFormat::RGBA8_UNorm:
            case PixelFormat::BGRA8_UNorm: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4 };
            case PixelFormat::RGBA8_SRGB:
            case PixelFormat::BGRA8_SRGB: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .isSRGB = true };
            case PixelFormat::D32_Float: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .isDepth = true };
            default: return {};
        }
    }

}