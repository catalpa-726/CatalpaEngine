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

        // 后端环境的创建与逆序释放
        bool Init(Window& window);
        void Shutdown();

        // 借用后端持有的对象
        const VulkanInstance& GetInstance() const { return m_Instance; }
        const VulkanPhysicalDevice& GetPhysicalDevice() const { return m_PhysicalDevice; }
        const VulkanSurface& GetSurface() const { return m_Surface; }

    private:
        // 实例和表面自有，物理设备句柄为借用。
        VulkanInstance m_Instance;
        VulkanSurface m_Surface;
        VulkanPhysicalDevice m_PhysicalDevice;
        bool m_Started = false;
    };

    // 集成 instance -> surface -> physical device 的创建，最后创建 device
    std::unique_ptr<RHIDevice> CreateVulkanDevice(Window& window, RHIStatus& status);

}