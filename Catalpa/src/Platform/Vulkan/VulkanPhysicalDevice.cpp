#include "catpch.h"
#include "VulkanPhysicalDevice.h"
#include "Log/Log.h"

namespace Catalpa {

    template<typename T>
    static VkResult EnumerateVulkan(std::vector<T>& values, auto query)
    {
        for (;;) {
            uint32_t count = 0;
            if (const VkResult result = query(&count, nullptr); result != VK_SUCCESS) return result;

            values.resize(count);
            if (count == 0) return VK_SUCCESS;

            const VkResult result = query(&count, values.data());
            if (result == VK_INCOMPLETE) continue;
            if (result == VK_SUCCESS) values.resize(count);
            return result;
        }
    }

    static VkResult FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t& graphicsFamily, uint32_t& presentFamily)
    {
        graphicsFamily = presentFamily = VK_QUEUE_FAMILY_IGNORED;

        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
        families.resize(count);

        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queueCount == 0) continue;

            VkBool32 present = VK_FALSE;
            if (const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present); result != VK_SUCCESS) return result;

            const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

            if (graphics && present) {
                graphicsFamily = presentFamily = i;
                return VK_SUCCESS;
            }

            if (graphics && graphicsFamily == VK_QUEUE_FAMILY_IGNORED) graphicsFamily = i;
            if (present && presentFamily == VK_QUEUE_FAMILY_IGNORED) presentFamily = i;
        }

        return VK_SUCCESS;
    }

    bool VulkanPhysicalDevice::Init(VkInstance instance, VkSurfaceKHR surface)
    {
        CAT_CORE_ASSERT(m_PhysicalDevice == VK_NULL_HANDLE, "Physical device already selected!");

        std::vector<VkPhysicalDevice> devices;
        const VkResult enumerateResult = EnumerateVulkan(devices, [instance](uint32_t* count, VkPhysicalDevice* data) {
            return vkEnumeratePhysicalDevices(instance, count, data);
            });

        if (enumerateResult != VK_SUCCESS) {
            CAT_CORE_ERROR("Failed to enumerate physical devices. VkResult: {0}", static_cast<int>(enumerateResult));
            return false;
        }

        for (VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;

            VkPhysicalDeviceVulkan13Features features13{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
            };

            VkPhysicalDeviceFeatures2 features{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &features13
            };

            vkGetPhysicalDeviceFeatures2(device, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;

            std::vector<VkExtensionProperties> extensions;
            const VkResult extensionResult = EnumerateVulkan(extensions, [device](uint32_t* count, VkExtensionProperties* data) {
                return vkEnumerateDeviceExtensionProperties(device, nullptr, count, data);
                });

            if (extensionResult != VK_SUCCESS) {
                CAT_CORE_ERROR("Failed to enumerate device extensions. VkResult: {0}", static_cast<int>(extensionResult));
                return false;
            }

            const bool supportsSwapchain = std::ranges::any_of(extensions, [](const VkExtensionProperties& extension) {
                return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                });

            if (!supportsSwapchain) continue;

            uint32_t formatCount = 0;
            if (const VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr); result != VK_SUCCESS) {
                CAT_CORE_ERROR("Failed to query surface formats. VkResult: {0}", static_cast<int>(result));
                return false;
            }

            uint32_t presentModeCount = 0;
            if (const VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr); result != VK_SUCCESS) {
                CAT_CORE_ERROR("Failed to query present modes. VkResult: {0}", static_cast<int>(result));
                return false;
            }

            if (formatCount == 0 || presentModeCount == 0) continue;

            uint32_t graphicsFamily, presentFamily;
            if (const VkResult result = FindQueueFamilies(device, surface, graphicsFamily, presentFamily); result != VK_SUCCESS) {
                CAT_CORE_ERROR("Failed to query presentation support. VkResult: {0}", static_cast<int>(result));
                return false;
            }

            if (graphicsFamily == VK_QUEUE_FAMILY_IGNORED || presentFamily == VK_QUEUE_FAMILY_IGNORED) continue;

            m_PhysicalDevice = device;
            m_Properties = properties;
            m_GraphicsQueueFamily = graphicsFamily;
            m_PresentQueueFamily = presentFamily;

            CAT_CORE_INFO("Selected Vulkan device: {0}", properties.deviceName);
            return true;
        }

        CAT_CORE_ERROR("No suitable Vulkan physical device found.");
        return false;
    }

}