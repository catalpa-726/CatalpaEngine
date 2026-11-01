#pragma once

#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanPhysicalDevice.h"

#include "RHI/RHIDevice.h"

namespace Catalpa {

    class Window;

    class VulkanBackend {
    public:
        VulkanBackend() = default;
        ~VulkanBackend();

        VulkanBackend(const VulkanBackend&) = delete;
        VulkanBackend& operator=(const VulkanBackend&) = delete;

        bool Init(Window& window);
        void Shutdown();

        const VulkanInstance& GetInstance() const { return m_Instance; }
        const VulkanPhysicalDevice& GetPhysicalDevice() const { return m_PhysicalDevice; }
        const VulkanSurface& GetSurface() const { return m_Surface; }

    private:
        VulkanInstance m_Instance;
        VulkanSurface m_Surface;
        VulkanPhysicalDevice m_PhysicalDevice;
        bool m_Started = false;
    };

    std::unique_ptr<RHIDevice> CreateVulkanDevice(Window& window, RHIStatus& status);

}