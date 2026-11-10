#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

    class Window;

    class VulkanSurface {
    public:
        VulkanSurface() = default;
        ~VulkanSurface();

        VulkanSurface(const VulkanSurface&) = delete;
        VulkanSurface& operator=(const VulkanSurface&) = delete;

        static bool GetRequiredExtensions(std::vector<const char*>& extensions);

        bool Init(VkInstance instance, Window& window);
        bool InitNative(VkInstance instance, void* nativeWindow);
        void Shutdown();

        VkSurfaceKHR GetHandle() const { return m_Surface; }

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    };

}