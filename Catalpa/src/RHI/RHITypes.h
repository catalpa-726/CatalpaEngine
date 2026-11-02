#pragma once

namespace Catalpa {

    struct Extent2D {
        unsigned int width = 0;
        unsigned int height = 0;

        [[nodiscard]] constexpr bool IsEmpty() const noexcept { return width == 0 || height == 0; }
        constexpr bool operator==(const Extent2D&) const = default;
    };

}