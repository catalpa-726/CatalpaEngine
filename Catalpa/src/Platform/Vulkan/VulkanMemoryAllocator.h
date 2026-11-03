#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include <vk_mem_alloc.h>

namespace Catalpa {

    class VulkanMemoryAllocator {
    public:
        VulkanMemoryAllocator() = default;
        ~VulkanMemoryAllocator();

        VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
        VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;

        // 在逻辑设备和 GLAD 函数加载完成后初始化。
        [[nodiscard]] RHIStatus Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);

        // 调用前释放全部 allocation；本函数不等待 GPU。
        void Shutdown();

        VmaAllocator GetHandle() const { return m_Allocator; }

    private:
        VmaAllocator m_Allocator = VK_NULL_HANDLE;
    };

}