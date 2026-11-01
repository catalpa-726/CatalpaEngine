#pragma once

#include "Core/Base.h"
#include "RHIBackend.h"
#include "RHIError.h"

namespace Catalpa {

    class CATALPA_API RHIDevice {
    public:
        virtual ~RHIDevice() = default;

        virtual GraphicsBackend GetBackend() const = 0;

        // 设备必须处于有效状态，调用期间停止向队列提交工作。
        [[nodiscard]] virtual RHIStatus WaitIdle() const = 0;

        // 调用前结束 GPU 使用，并释放设备创建的子资源。
        virtual void Shutdown() = 0;
    };

}