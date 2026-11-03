#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHISwapchainDesc.h"
#include "RHIHandle.h"
#include "RHICompletionPoint.h"

// 提供上层能够持有的交换链接口
namespace Catalpa {

    class CATALPA_API RHISwapchain {
    public:
        virtual ~RHISwapchain() = default;

        // 查询实际尺寸、格式、图像数量和状态
        virtual const RHISwapchainInfo& GetInfo() const = 0;

        // 调用前结束 GPU 使用并释放命令引用；按设备能力完成呈现等待或兼容收尾。
        // 设备空闲兼容路径不具备呈现资源回收的完整规范保证。
        [[nodiscard]] virtual RHIStatus Recreate(const RHISwapchainDesc& desc) = 0;
        virtual void Shutdown() = 0;

        // imageIndex 索引当前交换链图像；返回值是独立的资源句柄。
        virtual TextureViewHandle GetImageViewHandle(uint32_t imageIndex) const = 0;

        // 使用已准备好的帧槽获取图像；timeout 单位为纳秒。
        [[nodiscard]] virtual RHIStatus Acquire(RHIFrameToken frame, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired) = 0;

        // 对应提交成功后，将图像交给呈现队列。
        [[nodiscard]] virtual RHIStatus Present(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome) = 0;

        // 仅在 RHICapabilities::presentFence 为 true 时调用；不支持时返回 Unsupported。
        [[nodiscard]] virtual RHIStatus WaitForPresentCompletion() = 0;

        // 收尾尚未提交的获取；成功后仍要等待返回的完成点。
        [[nodiscard]] virtual RHIStatus CancelAcquire(RHIAcquireResult& acquired, RHICompletionPoint& completion) = 0;
    };

}