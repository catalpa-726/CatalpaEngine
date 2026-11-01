#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

    class VulkanLoader {
    public:
        static bool Load(
            VkInstance instance = VK_NULL_HANDLE,
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE,
            VkDevice device = VK_NULL_HANDLE);

        static void Shutdown();
    };

}