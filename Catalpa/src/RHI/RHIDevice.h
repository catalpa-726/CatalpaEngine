#pragma once

#include "Core/Base.h"
#include "RHIBackend.h"
#include "RHISwapchain.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHICapabilities.h"
#include "RHIQueue.h"
#include "RHIPipelineDesc.h"
#include "RHIBufferDesc.h"

namespace Catalpa {

    class CATALPA_API RHIDevice {
    public:
        virtual ~RHIDevice() = default;

        // 后端身份、设备等待与交换链创建。
        virtual GraphicsBackend GetBackend() const = 0;

        // 设备必须处于有效状态，调用期间停止向队列提交工作。
        // 设备空闲不等同于呈现 Fence 的资源回收证明。
        [[nodiscard]] virtual RHIStatus WaitIdle() const = 0;

        // 返回的交换链必须先于设备释放；失败时返回空指针并填写 status。
        [[nodiscard]] virtual std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status) = 0;

        // 查询初始化时保存的设备上限与实际启用的可选能力。
        virtual const RHICapabilities& GetCapabilities() const = 0;

        // 查询普通 GPU 纹理的单项格式能力；未知格式返回全 false。
        virtual RHIFormatSupport GetFormatSupport(PixelFormat format) const = 0;

        // 调用前结束 GPU 使用，并释放设备创建的子资源。
        virtual void Shutdown() = 0;

        virtual DeviceId GetDeviceId() const = 0;

        // 调用前停止录制和提交，并收尾尚未提交的 Acquire。
        // 等待设备后，丢弃设备管理的帧命令及其资源引用。
        // DeviceLost 表示进入失败关闭，不能继续使用该设备。
        [[nodiscard]] virtual RHIStatus WaitIdleAndDiscardFrameCommands() = 0;

        // 图形队列与帧槽准备。
        virtual RHIQueue& GetGraphicsQueue() = 0;
        virtual uint32_t GetFrameCount() const = 0;

        // 等待指定帧槽，再重置它的命令资源；超时通过 ready == false 表示。
        [[nodiscard]] virtual RHIStatus PrepareFrame(uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& frame, bool& ready) = 0;

        // 创建 Buffer；成功后由设备登记并管理原生资源。
        [[nodiscard]] virtual RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer) = 0;

        // 同步复制 CPU 数据并完成必要的 flush；当前拒绝写入仍被命令引用的 Buffer。
        [[nodiscard]] virtual RHIStatus WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data) = 0;

        // 释放逻辑所有权；已有命令引用继续保留原生资源。
        virtual void ReleaseBuffer(BufferHandle buffer) = 0;

        // 同步创建管线；成功后由设备管理原生资源。
        [[nodiscard]] virtual RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline) = 0;

        // 释放逻辑所有权；已有命令引用会继续保留原生管线。
        virtual void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;
    };

}