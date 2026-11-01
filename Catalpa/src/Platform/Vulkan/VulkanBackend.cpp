#include "catpch.h"
#include "VulkanBackend.h"
#include "VulkanLoader.h"
#include "VulkanDevice.h"
#include "Log/Log.h"

namespace Catalpa {

    VulkanBackend::~VulkanBackend() { Shutdown(); }

    bool VulkanBackend::Init(Window& window)
    {
        if (m_Started) {
            CAT_CORE_ASSERT(false, "Vulkan backend already started!");
            return false;
        }

        std::vector<const char*> extensions;
        if (!VulkanSurface::GetRequiredExtensions(extensions)) return false;

        m_Started = true;

        if (!m_Instance.Init(extensions) ||
            !m_Surface.Init(m_Instance.GetHandle(), window) ||
            !m_PhysicalDevice.Init(m_Instance.GetHandle(), m_Surface.GetHandle())) {
            Shutdown();
            return false;
        }

        CAT_CORE_INFO("Vulkan backend initialized.");
        return true;
    }

    void VulkanBackend::Shutdown()
    {
        if (!m_Started) return;

        // GPU 等待和设备子资源回收由 VulkanDevice 在销毁逻辑设备前完成。
        m_PhysicalDevice = VulkanPhysicalDevice{};
        m_Surface.Shutdown();
        m_Instance.Shutdown();
        VulkanLoader::Shutdown();

        m_Started = false;
    }

    std::unique_ptr<RHIDevice> CreateVulkanDevice(Window& window, RHIStatus& status)
    {
        status = {};

        auto backend = std::make_unique<VulkanBackend>();

        if (!backend->Init(window)) {
            status = { .code = RHIErrorCode::BackendFailure, .operation = "VulkanBackend::Init" };
            return nullptr;
        }

        auto device = std::make_unique<VulkanDevice>();

        if (!device->Init(std::move(backend))) {
            status = { .code = RHIErrorCode::BackendFailure, .operation = "VulkanDevice::Init" };
            return nullptr;
        }

        return device;
    }

}