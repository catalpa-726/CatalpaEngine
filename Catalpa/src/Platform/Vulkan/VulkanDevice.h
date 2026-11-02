#pragma once

#include "RHI/RHIDevice.h"
#include "VulkanHeaders.h"

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

        VulkanQueue& GetGraphicsQueue() { return *m_GraphicsQueue; }
        VkQueue GetPresentQueue() const { return m_PresentQueue; }

        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

        VulkanFrameContext& GetFrameContext(uint32_t frameSlot) { return *m_Frames[frameSlot]; }

        DeviceId GetDeviceId() const override;

        VulkanResourceRegistry& GetResourceRegistry() { return *m_ResourceRegistry; }
        const VulkanResourceRegistry& GetResourceRegistry() const { return *m_ResourceRegistry; }

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
    };

}