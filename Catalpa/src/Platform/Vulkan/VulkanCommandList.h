#pragma once

#include "VulkanHeaders.h"
#include "VulkanDescriptorAllocator.h"
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
        explicit VulkanCommandList(VulkanDevice& device) : m_Device(device), m_Descriptors(device) {}
        ~VulkanCommandList() override;

        VulkanCommandList(const VulkanCommandList&) = delete;
        VulkanCommandList& operator=(const VulkanCommandList&) = delete;

        // 初始化与录制边界
        [[nodiscard]] RHIStatus Init(VulkanCommandPool& pool);
        [[nodiscard]] RHIStatus Begin() override;
        [[nodiscard]] RHIStatus End() override;

        [[nodiscard]] RHIStatus SetResourceStatePlan(
            std::span<const RHIBufferStateUpdate> buffers,
            std::span<const RHITextureStateUpdate> textures) override;

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
        [[nodiscard]] RHIStatus SetGraphicsPushConstants(std::span<const std::byte> data) override;

        // 校验完整绑定布局，分配新 set，并保留其引用资源。
        [[nodiscard]] RHIStatus SetGraphicsBindings(std::span<const RHIBinding> bindings) override;
        [[nodiscard]] RHIStatus Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;

        [[nodiscard]] RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier> barriers) override;
        [[nodiscard]] RHIStatus CopyBuffer(BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy) override;
        // 录制整幅颜色纹理上传；不隐式生成访问屏障。
        [[nodiscard]] RHIStatus CopyBufferToTexture(BufferHandle source, TextureViewHandle destination, const RHIBufferTextureCopy& copy) override;
        [[nodiscard]] RHIStatus BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0) override;
        [[nodiscard]] RHIStatus BindIndexBuffer(BufferHandle buffer, RHIIndexType type, uint64_t offset = 0) override;
        [[nodiscard]] RHIStatus DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

        [[nodiscard]] bool BeginDebugLabel(const char* name, const std::array<float, 4>& color) override;
        void EndDebugLabel() override;

    private:
        struct LocalBufferState {
            BufferHandle buffer{};
            RHIBufferStateSnapshot snapshot;
        };
        // 首次从权威快照导入；以后只由实际录制的barrier/copy更新，绝不使用整图最终状态。
        [[nodiscard]] RHIStatus GetLocalBufferState(BufferHandle buffer, LocalBufferState*& state);

        // 录制期资源引用与状态复位
        [[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle);

        // 释放 first 之后追加的引用；默认释放全部。
        void ReleaseTextureViews(size_t first = 0) noexcept;

        // 命令列表对 Sampler 去重保活。
        [[nodiscard]] RHIStatus RetainSampler(SamplerHandle sampler);

        // 归还 first 之后追加的命令引用。
        void ReleaseSamplers(size_t first = 0) noexcept;

        // 只在原生命令池重置成功后调用；描述符重置失败时保留资源引用。
        [[nodiscard]] RHIStatus OnPoolReset();

        // 失效不等于 GPU 使用已经结束，因此这里不释放引用。
        void Invalidate() { m_State = RHICommandListState::Invalid; }

        // 在命令池安全重置或销毁后，归还全部管线引用。
        void ReleasePipelines() noexcept;

        [[nodiscard]] RHIStatus RetainBuffer(BufferHandle handle);
        void ReleaseBuffers(size_t first = 0) noexcept;

        // 提交前检查版本和资源声明覆盖。
        [[nodiscard]] RHIStatus ValidateResourceStatePlan() const noexcept;

        // 仅由 Queue 在原生提交成功后发布状态。
        void CommitResourceStatePlan() noexcept;

        // 必须恰好导出当前 Acquire 的图像到 Present；允许同时更新普通纹理。
        bool ExportsPresent(TextureViewHandle view) const noexcept;

    private:
        // 帧复用与队列提交负责内部状态变更
        friend class VulkanFrameContext;
        friend class VulkanQueue;

        // 原生命令缓冲及录制状态
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        RHICommandListState m_State = RHICommandListState::Uninitialized;

        uint32_t m_DebugLabelDepth = 0;

        // 借用设备，并保留录制所引用的资源。
        VulkanDevice& m_Device;
        VulkanDescriptorAllocator m_Descriptors;
        std::vector<SamplerHandle> m_RetainedSamplers;
        std::vector<TextureViewHandle> m_RetainedViews;
        std::vector<GraphicsPipelineHandle> m_RetainedPipelines;

        // 通过命令引用保活的管线布局，并记录参数是否准备好
        VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;
        uint32_t m_PushConstantBytes = 0;
        VkShaderStageFlags m_PushConstantStages = 0;
        bool m_PushConstantsSet = false;

        VkDescriptorSetLayout m_BoundBindingLayout = VK_NULL_HANDLE;
        std::array<RHIBindingLayoutEntry, RHIMaxBindingsPerSet> m_BoundBindings{};
        size_t m_BoundBindingCount = 0;
        bool m_GraphicsBindingsSet = false;

        // 当前管线、渲染附件与动态状态
        PixelFormat m_BoundColorFormat = PixelFormat::Unknown;
        PixelFormat m_RenderingColorFormat = PixelFormat::Unknown;
        PixelFormat m_BoundDepthFormat = PixelFormat::Unknown;
        PixelFormat m_RenderingDepthFormat = PixelFormat::Unknown;
        bool m_BoundDepthWrite = false;
        bool m_RenderingDepthReadOnly = false;
        bool m_ViewportAndScissorSet = false;

        std::vector<BufferHandle> m_RetainedBuffers;
        // 命令局部访问/初始化账本，不推导依赖、不发布全局已接受状态。
        std::vector<LocalBufferState> m_LocalBufferStates;

        std::vector<RHIBufferStateUpdate> m_BufferStateUpdates;
        std::vector<RHITextureStateUpdate> m_TextureStateUpdates;
        bool m_ResourceStatePlanBound = false;
        bool m_ResourceStatePlanCommitted = false;

        uint32_t m_VertexStride = 0;
        uint32_t m_VertexAttributeEnd = 0;
        uint64_t m_VertexBufferBytes = 0;
        uint64_t m_IndexBufferBytes = 0;
        uint32_t m_IndexSize = 0;
        bool m_VertexBufferBound = false;
        bool m_IndexBufferBound = false;
    };

}