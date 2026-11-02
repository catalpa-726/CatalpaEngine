#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHISwapchainDesc.h"

// 提供上层能够持有的交换链接口
namespace Catalpa {

    class CATALPA_API RHISwapchain {
    public:
        virtual ~RHISwapchain() = default;

        // 查询实际尺寸、格式、图像数量和状态
        virtual const RHISwapchainInfo& GetInfo() const = 0;

        // 调用前结束旧交换链资源的 GPU 和呈现使用。
        [[nodiscard]] virtual RHIStatus Recreate(const RHISwapchainDesc& desc) = 0;

        // 调用前结束交换链资源的 GPU 和呈现使用。
        virtual void Shutdown() = 0;
    };

}