#pragma once

#include "RHITypes.h"
#include "Images/PixelFormat.h"
#include "RHIHandle.h"
#include "RHIGpuDependency.h"

namespace Catalpa {

    enum class RHIPresentMode { Fifo, Mailbox, Immediate };
    enum class RHISwapchainState { Uninitialized, Ready, Suspended, OutOfDate, SurfaceLost };
    enum class RHIAcquireOutcome { Acquired, Suboptimal, NotReady, Suspended, OutOfDate };
    enum class RHIPresentOutcome { Queued, Suboptimal, OutOfDate, SurfaceLost, NotQueued, Unknown };

    /*
    * 保存一次获取的图像与依赖身份
    * RHIAcquireResult：描述本次获取结果和图像索引；实际错误继续使用已有 RHIStatus
    * HasImage()：判断本次是否取得图像。它由结果计算，不额外保存一个容易失步的布尔字段。
    */
    struct RHIAcquireResult {
        RHIAcquireOutcome outcome = RHIAcquireOutcome::NotReady;
        uint32_t imageIndex = std::numeric_limits<uint32_t>::max();
        TextureViewHandle view{};
        RHIGpuDependency acquireReady{};
        RHIGpuDependency renderFinished{};

        [[nodiscard]] constexpr bool HasImage() const noexcept { return outcome == RHIAcquireOutcome::Acquired || outcome == RHIAcquireOutcome::Suboptimal; }
        constexpr bool operator==(const RHIAcquireResult&) const = default;
    };

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