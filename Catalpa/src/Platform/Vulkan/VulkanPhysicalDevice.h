#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

    class VulkanPhysicalDevice {
    public:
        bool Init(VkInstance instance, VkSurfaceKHR surface, bool surfaceMaintenanceEnabled);

        VkPhysicalDevice GetHandle() const { return m_PhysicalDevice; }
        const VkPhysicalDeviceProperties& GetProperties() const { return m_Properties; }
        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

        bool SupportsSwapchainMaintenance() const { return m_SwapchainMaintenanceSupported; }

    private:
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_Properties{};
        uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        bool m_SwapchainMaintenanceSupported = false;
    };

}