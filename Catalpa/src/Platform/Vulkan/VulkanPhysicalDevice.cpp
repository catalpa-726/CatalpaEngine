#include "catpch.h"
#include "Log/Log.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

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

    bool VulkanPhysicalDevice::Init(VkInstance instance, VkSurfaceKHR surface, bool surfaceMaintenanceEnabled)
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

            std::vector<VkExtensionProperties> extensions;
            const VkResult extensionResult = EnumerateVulkan(extensions, [device](uint32_t* count, VkExtensionProperties* data) {
                return vkEnumerateDeviceExtensionProperties(device, nullptr, count, data);
            });

            if (extensionResult != VK_SUCCESS) {
                CAT_CORE_ERROR("Failed to enumerate device extensions. VkResult: {0}", static_cast<int>(extensionResult));
                return false;
            }

            const auto supportsExtension = [&](const char* name) {
                return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
                    return std::string_view(extension.extensionName) == name;
                });
            };

            if (!supportsExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

            const bool supportsMaintenance = surfaceMaintenanceEnabled && supportsExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

            VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
            VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = supportsMaintenance ? &maintenance : nullptr };
            VkPhysicalDeviceFeatures2 features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features13 };

            vkGetPhysicalDeviceFeatures2(device, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;

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
            m_SwapchainMaintenanceSupported = supportsMaintenance && maintenance.swapchainMaintenance1;

            CAT_CORE_INFO("Selected Vulkan device: {0}", properties.deviceName);
            return true;
        }

        CAT_CORE_ERROR("No suitable Vulkan device found: Vulkan 1.3, dynamic rendering, synchronization2 and swapchain support are required.");
        return false;
    }

}