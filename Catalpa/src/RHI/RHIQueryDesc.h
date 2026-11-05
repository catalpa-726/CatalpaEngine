#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"
#include <cstdint>
#include <span>

namespace Catalpa {

    // 当前明确提供时间戳查询；结果可用不等于包含它的整批提交已完成。
    struct RHIQueryDesc { uint32_t count = 2; };
    struct RHIQueryResult { uint64_t value = 0; bool available = false; };

    class CATALPA_API RHIQueryPool {
    public:
        virtual ~RHIQueryPool() = default;
        virtual DeviceId GetDeviceId() const noexcept = 0;
        virtual uint32_t GetCount() const noexcept = 0;
        virtual uint32_t GetTimestampValidBits() const noexcept = 0;
        virtual float GetTimestampPeriodNanoseconds() const noexcept = 0;
        // 不带 WAIT；调用方分别检查每项 available 和自己的帧/提交身份。
        [[nodiscard]] virtual RHIStatus GetResults(uint32_t first, std::span<RHIQueryResult> results) = 0;
    };
}
