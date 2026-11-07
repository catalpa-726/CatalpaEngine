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

    RHIStatus VulkanMemoryAllocator::Init(VkInstance instance, VkPhysicalDevice physicalDevice,
                                          VkDevice device, bool memoryBudgetExtension) {
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

        const VmaAllocatorCreateInfo info{.flags = memoryBudgetExtension
                                                       ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT
                                                       : VmaAllocatorCreateFlags(0),
                                          .physicalDevice = physicalDevice,
                                          .device = device,
                                          .pVulkanFunctions = &functions,
                                          .instance = instance,
                                          .vulkanApiVersion = VK_API_VERSION_1_3};

        const auto status = MakeVulkanStatus(vmaCreateAllocator(&info, &m_Allocator), "vmaCreateAllocator");
        if (status.IsSuccess())
            m_DriverBudget = memoryBudgetExtension;
        return status;
    }

    RHIStatus VulkanMemoryAllocator::GetBudgets(std::vector<RHIMemoryHeapBudget>& output) const try {
        if (!m_Allocator)
            return {RHIErrorCode::InvalidState, 0, "VMA/GetBudgets"};
        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
        vmaGetHeapBudgets(m_Allocator, budgets.data());
        const VkPhysicalDeviceMemoryProperties* properties = nullptr;
        vmaGetMemoryProperties(m_Allocator, &properties);
        std::vector<RHIMemoryHeapBudget> result;
        result.reserve(properties->memoryHeapCount);
        for (uint32_t i = 0; i < properties->memoryHeapCount; ++i) {
            const auto& heap = properties->memoryHeaps[i];
            const auto& value = budgets[i];
            result.push_back({heap.size, value.budget, value.usage, value.statistics.allocationBytes,
                              value.statistics.blockBytes,
                              (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0, m_DriverBudget});
        }
        output = std::move(result);
        return {};
    } catch (const std::bad_alloc&) {
        return {RHIErrorCode::OutOfMemory, 0, "VMA/BudgetSnapshot"};
    }

    void VulkanMemoryAllocator::Shutdown()
    {
        if (m_Allocator == VK_NULL_HANDLE) return;

        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
    }

} // namespace Catalpa
