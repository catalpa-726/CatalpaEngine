#pragma once

#include "Core/Base.h"
#include "RHIBackend.h"
#include "RHISwapchain.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHICapabilities.h"

namespace Catalpa {

    class CATALPA_API RHIDevice {
    public:
        virtual ~RHIDevice() = default;

        virtual GraphicsBackend GetBackend() const = 0;

        // 设备必须处于有效状态，调用期间停止向队列提交工作。
        [[nodiscard]] virtual RHIStatus WaitIdle() const = 0;

        // 返回的交换链必须先于设备释放；失败时返回空指针并填写 status。
        [[nodiscard]] virtual std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status) = 0;

        // 查询初始化时保存的设备上限。
        virtual const RHICapabilities& GetCapabilities() const = 0;

        // 查询普通 GPU 纹理的单项格式能力；未知格式返回全 false。
        virtual RHIFormatSupport GetFormatSupport(PixelFormat format) const = 0;

        // 调用前结束 GPU 使用，并释放设备创建的子资源。
        virtual void Shutdown() = 0;

        virtual DeviceId GetDeviceId() const = 0;
    };

}