#pragma once

#include "RHI/RHIDevice.h"
#include "VulkanHeaders.h"
#include "VulkanQueue.h"

namespace Catalpa {

    class VulkanBackend;
    class VulkanFrameContext;
    class VulkanQueue;
    class VulkanResourceRegistry;

    class VulkanDevice : public RHIDevice {
    public:
        static constexpr uint32_t FramesInFlight = 2;

    public:
        VulkanDevice();
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        bool Init(std::unique_ptr<VulkanBackend> backend);
        GraphicsBackend GetBackend() const override { return GraphicsBackend::Vulkan; }
        [[nodiscard]] RHIStatus WaitIdle() const override;

        [[nodiscard]] std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status) override;

        // 调用前结束 GPU 使用，并释放设备创建的子资源。
        void Shutdown() override;

        VkDevice GetHandle() const { return m_Device; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }

        VkQueue GetPresentQueue() const { return m_PresentQueue; }
        VulkanQueue& GetGraphicsQueue() override { return *m_GraphicsQueue; }
        uint32_t GetFrameCount() const override { return FramesInFlight; }

        [[nodiscard]] RHIStatus PrepareFrame(uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& frame, bool& ready) override;

        // 后端统一解析设备、帧槽及代际。
        VulkanFrameContext* ResolveFrame(RHIFrameToken token);

        // 只登记失设备状态，不在当前调用栈内销毁资源。
        RHIStatus ObserveStatus(RHIStatus status);
        const RHIStatus& GetFailure() const { return m_DeviceFailure; }

        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

        VulkanFrameContext& GetFrameContext(uint32_t frameSlot) { return *m_Frames[frameSlot]; }

        DeviceId GetDeviceId() const override;

        VulkanResourceRegistry& GetResourceRegistry() { return *m_ResourceRegistry; }
        const VulkanResourceRegistry& GetResourceRegistry() const { return *m_ResourceRegistry; }

        const RHICapabilities& GetCapabilities() const override { return m_Capabilities; }
        RHIFormatSupport GetFormatSupport(PixelFormat format) const override;

        [[nodiscard]] RHIStatus WaitIdleAndDiscardFrameCommands() override;

    private:
        std::unique_ptr<VulkanBackend> m_Backend;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;

        std::unique_ptr<VulkanQueue> m_GraphicsQueue;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;

        uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        std::array<std::unique_ptr<VulkanFrameContext>, FramesInFlight> m_Frames{};

        std::unique_ptr<VulkanResourceRegistry> m_ResourceRegistry;

        RHICapabilities m_Capabilities{};

        RHIStatus m_DeviceFailure{};
    };

}