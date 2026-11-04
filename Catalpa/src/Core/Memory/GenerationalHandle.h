#pragma once

#include <cstdint>
#include <limits>

// 负责槽位索引和槽位代次，不认识设备或 Vulkan
namespace Catalpa {

    template<typename Tag>
    struct GenerationalHandle {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint32_t index = InvalidIndex;
        uint32_t generation = 0;

        [[nodiscard]] constexpr bool HasValue() const noexcept { return index != InvalidIndex && generation != 0; }
        constexpr bool operator==(const GenerationalHandle&) const = default;
    };

}
