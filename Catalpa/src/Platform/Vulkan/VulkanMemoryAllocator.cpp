#include "catpch.h"
#include "Log/Log.h"

#define VMA_IMPLEMENTATION
#define VMA_ASSERT(condition) do { CAT_CORE_ASSERT((condition), #condition); } while (false)
#include "VulkanMemoryAllocator.h"
#undef VMA_ASSERT
#undef VMA_IMPLEMENTATION

#include "VulkanUtils.h"

namespace Catalpa {

    VulkanMemoryAllocator::~VulkanMemoryAllocator() { Shutdown(); }

    RHIStatus VulkanMemoryAllocator::Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
    {
        if (m_Allocator != VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanMemoryAllocator::Init" };

        const VmaVulkanFunctions functions{
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
            .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
            .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
            .vkAllocateMemory = vkAllocateMemory,
            .vkFreeMemory = vkFreeMemory,
            .vkMapMemory = vkMapMemory,
            .vkUnmapMemory = vkUnmapMemory,
            .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
            .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
            .vkBindBufferMemory = vkBindBufferMemory,
            .vkBindImageMemory = vkBindImageMemory,
            .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
            .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
            .vkCreateBuffer = vkCreateBuffer,
            .vkDestroyBuffer = vkDestroyBuffer,
            .vkCreateImage = vkCreateImage,
            .vkDestroyImage = vkDestroyImage,
            .vkCmdCopyBuffer = vkCmdCopyBuffer,
            .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
            .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
            .vkBindBufferMemory2KHR = vkBindBufferMemory2,
            .vkBindImageMemory2KHR = vkBindImageMemory2,
            .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
            .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
            .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements
        };

        const VmaAllocatorCreateInfo info{
            .physicalDevice = physicalDevice,
            .device = device,
            .pVulkanFunctions = &functions,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_3
        };

        return MakeVulkanStatus(vmaCreateAllocator(&info, &m_Allocator), "vmaCreateAllocator");
    }

    void VulkanMemoryAllocator::Shutdown()
    {
        if (m_Allocator == VK_NULL_HANDLE) return;

        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
    }

}