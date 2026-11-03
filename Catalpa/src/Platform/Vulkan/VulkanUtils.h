#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "Log/Log.h"

// 集中复用已有的枚举和错误转换逻辑
namespace Catalpa {

    template<typename T>
    VkResult EnumerateVulkan(std::vector<T>& values, auto query)
    {
        for (;;) {
            uint32_t count = 0;
            if (const VkResult result = query(&count, nullptr); result != VK_SUCCESS) return result;

            values.resize(count);
            if (count == 0) return VK_SUCCESS;

            const VkResult result = query(&count, values.data());
            if (result == VK_INCOMPLETE) continue;
            if (result == VK_SUCCESS) values.resize(count);
            return result;
        }
    }

    inline RHIStatus MakeVulkanStatus(VkResult result, const char* operation)
    {
        if (result == VK_SUCCESS) return {};

        RHIErrorCode code = RHIErrorCode::BackendFailure;

        switch (result) {
        case VK_ERROR_DEVICE_LOST: code = RHIErrorCode::DeviceLost; break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: code = RHIErrorCode::OutOfMemory; break;
        case VK_ERROR_FORMAT_NOT_SUPPORTED: code = RHIErrorCode::Unsupported; break;
        default: break;
        }

        CAT_CORE_ERROR("{0} failed. VkResult: {1}", operation, static_cast<int>(result));
        return { .code = code, .nativeCode = static_cast<int>(result), .operation = operation };
    }

}