#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandList.h"
#include "Images/PixelFormat.h"

// 实现 Vulkan 命令录制，借用命令池分配的缓冲
namespace Catalpa {

    class VulkanCommandPool;
    class VulkanFrameContext;
    class VulkanDevice;

    class VulkanCommandList : public RHICommandList {
    public:
        // 构造与命令缓冲借用
        explicit VulkanCommandList(VulkanDevice& device) : m_Device(device) {}
        ~VulkanCommandList() override;

        VulkanCommandList(const VulkanCommandList&) = delete;
        VulkanCommandList& operator=(const VulkanCommandList&) = delete;

        // 初始化与录制边界
        [[nodiscard]] RHIStatus Init(VulkanCommandPool& pool);
        [[nodiscard]] RHIStatus Begin() override;
        [[nodiscard]] RHIStatus End() override;

        // 录制状态与提交登记
        RHICommandListState GetState() const override { return m_State; }
        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }

        // 仅在实际提交成功后调用。
        void MarkSubmitted();

        // 录制调用者提供的同步依赖；当前只允许在渲染段之外调用。
        [[nodiscard]] RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) override;

        // 使用 flags = 0、viewMask = 0 的普通动态渲染，当前仅支持单层单采样颜色附件。
        [[nodiscard]] RHIStatus BeginRendering(const RHIRenderingDesc& desc) override;
        [[nodiscard]] RHIStatus EndRendering() override;

        // 设置第一个视口和裁剪区域，坐标由调用者明确提供。
        [[nodiscard]] RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) override;

        // 管线绑定与绘制
        [[nodiscard]] RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        [[nodiscard]] RHIStatus Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;

    private:
        // 录制期资源引用与状态复位
        [[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle);

        // 释放 first 之后追加的引用；默认释放全部。
        void ReleaseTextureViews(size_t first = 0) noexcept;

        // 只在原生命令池重置成功后调用。
        void OnPoolReset();

        // 失效不等于 GPU 使用已经结束，因此这里不释放引用。
        void Invalidate() { m_State = RHICommandListState::Invalid; }

        // 在命令池安全重置或销毁后，归还全部管线引用。
        void ReleasePipelines() noexcept;

    private:
        // 帧复用与队列提交负责内部状态变更
        friend class VulkanFrameContext;
        friend class VulkanQueue;

        // 原生命令缓冲及录制状态
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        RHICommandListState m_State = RHICommandListState::Uninitialized;

        // 借用设备，并保留录制所引用的资源。
        VulkanDevice& m_Device;
        std::vector<TextureViewHandle> m_RetainedViews;
        std::vector<GraphicsPipelineHandle> m_RetainedPipelines;

        // 当前管线、渲染附件与动态状态
        PixelFormat m_BoundColorFormat = PixelFormat::Unknown;
        PixelFormat m_RenderingColorFormat = PixelFormat::Unknown;
        bool m_ViewportAndScissorSet = false;
    };

}