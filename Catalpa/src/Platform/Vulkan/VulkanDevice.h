#pragma once

#include "RHI/RHIDevice.h"
#include "VulkanHeaders.h"
#include "VulkanQueue.h"

namespace Catalpa {

    class VulkanBackend;
    class VulkanFrameContext;
    class VulkanQueue;
    class VulkanResourceRegistry;
    class VulkanMemoryAllocator;
    class VulkanDebugUtils;
    class VulkanPipelineCache;

    class VulkanDevice : public RHIDevice {
    public:
        static constexpr uint32_t FramesInFlight = 2;

    public:
        // 构造与所有权
        VulkanDevice();
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // 设备初始化、同步与交换链创建
        bool Init(std::unique_ptr<VulkanBackend> backend);
        GraphicsBackend GetBackend() const override { return GraphicsBackend::Vulkan; }
        [[nodiscard]] RHIStatus WaitIdle() const override;

        [[nodiscard]] std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status) override;

        // 调用前结束 GPU 使用，并释放外部持有的设备子资源；内部帧和队列由此回收。
        void Shutdown() override;

        // 原生对象、队列与帧槽
        VkDevice GetHandle() const { return m_Device; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkPipelineCache GetPipelineCache() const;

        VkQueue GetPresentQueue() const { return m_PresentQueue; }
        VulkanQueue& GetGraphicsQueue() override { return *m_GraphicsQueue; }
        uint32_t GetFrameCount() const override { return FramesInFlight; }

        [[nodiscard]] RHIStatus PrepareFrame(uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& frame, bool& ready) override;

        // 后端统一解析设备、帧槽及代际。
        VulkanFrameContext* ResolveFrame(RHIFrameToken token);

        // 只登记失设备状态，不在当前调用栈内销毁资源。
        RHIStatus ObserveStatus(RHIStatus status);
        const RHIStatus& GetFailure() const { return m_DeviceFailure; }

        // 后端队列族与帧访问
        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

        VulkanFrameContext& GetFrameContext(uint32_t frameSlot) { return *m_Frames[frameSlot]; }

        // 借用所属实例的诊断服务，不接管其生命周期。
        const VulkanDebugUtils& GetDebugUtils() const;

        // 资源登记与设备能力
        DeviceId GetDeviceId() const override;

        VulkanResourceRegistry& GetResourceRegistry() { return *m_ResourceRegistry; }
        const VulkanResourceRegistry& GetResourceRegistry() const { return *m_ResourceRegistry; }

        const RHICapabilities& GetCapabilities() const override { return m_Capabilities; }
        RHIFormatSupport GetFormatSupport(PixelFormat format) const override;

        VulkanMemoryAllocator& GetMemoryAllocator() { return *m_MemoryAllocator; }
        const VulkanMemoryAllocator& GetMemoryAllocator() const { return *m_MemoryAllocator; }

        // 关闭前收尾与管线管理
        [[nodiscard]] RHIStatus WaitIdleAndDiscardFrameCommands() override;

        [[nodiscard]] RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline) override;
        void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) override;

        // Buffer
        [[nodiscard]] RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer) override;
        [[nodiscard]] RHIStatus WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data) override;
        void ReleaseBuffer(BufferHandle buffer) override;

        [[nodiscard]] RHIStatus GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const override;
        [[nodiscard]] RHIStatus GetTextureState(TextureViewHandle view, RHITextureStateSnapshot& snapshot) const override;

        // 自有纹理及 View 的创建和逻辑所有权。
        [[nodiscard]] RHIStatus CreateTexture(const RHITextureDesc& desc, TextureHandle& texture) override;
        [[nodiscard]] RHIStatus CreateTextureView(TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view) override;
        void ReleaseTexture(TextureHandle texture) override;
        void ReleaseTextureView(TextureViewHandle view) override;

        // Sampler 创建和逻辑所有权。
        [[nodiscard]] RHIStatus CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler) override;
        void ReleaseSampler(SamplerHandle sampler) override;

    private:
        // 后端环境与设备句柄
        std::unique_ptr<VulkanBackend> m_Backend;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;

        // 队列及其所属队列族
        std::unique_ptr<VulkanQueue> m_GraphicsQueue;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;

        uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        // 帧资源先于登记表和逻辑设备释放
        std::array<std::unique_ptr<VulkanFrameContext>, FramesInFlight> m_Frames{};

        // 登记表内的资源释放完毕后，才能销毁分配器。
        std::unique_ptr<VulkanMemoryAllocator> m_MemoryAllocator;

        std::unique_ptr<VulkanResourceRegistry> m_ResourceRegistry;
        std::unique_ptr<VulkanPipelineCache> m_PipelineCache;

        // 已启用能力与终止性设备状态
        RHICapabilities m_Capabilities{};
        RHIStatus m_DeviceFailure{};
    };

}