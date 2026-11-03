#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandList.h"

// 实现 Vulkan 命令录制，借用命令池分配的缓冲
namespace Catalpa {

    class VulkanCommandPool;
    class VulkanFrameContext;
    class VulkanDevice;

    class VulkanCommandList : public RHICommandList {
    public:
        explicit VulkanCommandList(VulkanDevice& device) : m_Device(device) {}
        ~VulkanCommandList() override;

        VulkanCommandList(const VulkanCommandList&) = delete;
        VulkanCommandList& operator=(const VulkanCommandList&) = delete;

        [[nodiscard]] RHIStatus Init(VulkanCommandPool& pool);
        [[nodiscard]] RHIStatus Begin() override;
        [[nodiscard]] RHIStatus End() override;

        RHICommandListState GetState() const override { return m_State; }
        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }

        // 仅在实际提交成功后调用。
        void MarkSubmitted();

        // 录制调用者提供的同步依赖；当前只允许在渲染段之外调用。
        [[nodiscard]] RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) override;

        // 当前支持 flags = 0、viewMask = 0 的普通动态渲染。
        [[nodiscard]] RHIStatus BeginRendering(const RHIRenderingDesc& desc) override;
        [[nodiscard]] RHIStatus EndRendering() override;

        // 设置第一个视口和裁剪区域，坐标由调用者明确提供。
        [[nodiscard]] RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) override;

    private:
        [[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle);

        // 释放 first 之后追加的引用；默认释放全部。
        void ReleaseTextureViews(size_t first = 0) noexcept;

        // 只在原生命令池重置成功后调用。
        void OnPoolReset();

        // 失效不等于 GPU 使用已经结束，因此这里不释放引用。
        void Invalidate() { m_State = RHICommandListState::Invalid; }

    private:
        friend class VulkanFrameContext;
        friend class VulkanQueue;

        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        RHICommandListState m_State = RHICommandListState::Uninitialized;

        VulkanDevice& m_Device;
        std::vector<TextureViewHandle> m_RetainedViews;
    };

}