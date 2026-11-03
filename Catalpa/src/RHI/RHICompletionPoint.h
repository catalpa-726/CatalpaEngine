#pragma once

#include "RHITypes.h"

// 提供 CPU 可以查询的提交完成身份
namespace Catalpa {

    // sequence 只为实际接受的提交分配；不因帧槽或 Fence 复用而改变。
    struct RHICompletionPoint {
        DeviceId device{};
        QueueId queue{};
        uint64_t sequence = 0;

        [[nodiscard]] constexpr bool HasValue() const noexcept { return device.HasValue() && queue.HasValue() && sequence != 0; }
        constexpr bool operator==(const RHICompletionPoint&) const = default;
    };

}