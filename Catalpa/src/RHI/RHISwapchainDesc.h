#pragma once

#include "RHITypes.h"
#include "Images/PixelFormat.h"

namespace Catalpa {

    enum class RHIPresentMode { Fifo, Mailbox, Immediate };
    enum class RHISwapchainState { Uninitialized, Ready, Suspended, OutOfDate, SurfaceLost };

    struct RHISwapchainDesc {
        Extent2D extent{};
        PixelFormat preferredFormat = PixelFormat::BGRA8_SRGB;
        RHIPresentMode preferredPresentMode = RHIPresentMode::Fifo;
        unsigned int preferredImageCount = 3;
        constexpr bool operator==(const RHISwapchainDesc&) const = default;
    };

    struct RHISwapchainInfo {
        RHISwapchainState state = RHISwapchainState::Uninitialized;
        Extent2D extent{};
        PixelFormat format = PixelFormat::Unknown;
        RHIPresentMode presentMode = RHIPresentMode::Fifo;
        unsigned int imageCount = 0;
    };

}