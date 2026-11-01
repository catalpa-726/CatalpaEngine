#include "catpch.h"
#include "VulkanSurface.h"
#include "Core/Window.h"
#include "Log/Log.h"

#include <GLFW/glfw3.h>

namespace Catalpa {

    VulkanSurface::~VulkanSurface() { Shutdown(); }

    bool VulkanSurface::GetRequiredExtensions(std::vector<const char*>& extensions)
    {
        uint32_t count = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&count);

        if (!names) {
            CAT_CORE_ERROR("Failed to get GLFW Vulkan instance extensions.");
            return false;
        }

        extensions.assign(names, names + count);
        return true;
    }

    bool VulkanSurface::Init(VkInstance instance, Window& window)
    {
        CAT_CORE_ASSERT(m_Surface == VK_NULL_HANDLE, "Vulkan surface already exists!");

        auto* nativeWindow = static_cast<GLFWwindow*>(window.GetNativeWindow());
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        if (const VkResult result = glfwCreateWindowSurface(instance, nativeWindow, nullptr, &surface); result != VK_SUCCESS) {
            CAT_CORE_ERROR("Failed to create Vulkan surface. VkResult: {0}", static_cast<int>(result));
            return false;
        }

        m_Instance = instance;
        m_Surface = surface;
        return true;
    }

    void VulkanSurface::Shutdown()
    {
        if (m_Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
            m_Instance = VK_NULL_HANDLE;
        }
    }

}