#include "catpch.h"
#include "VulkanLoader.h"
#include "Log/Log.h"

namespace Catalpa {

    bool VulkanLoader::Load(
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device)
    {
        const int version = gladLoaderLoadVulkan(instance, physicalDevice, device);
        CAT_CORE_ASSERT(version != 0, "Failed to load Vulkan functions!");
        return version != 0;
    }

    void VulkanLoader::Shutdown()
    {
        gladLoaderUnloadVulkan();
    }

}