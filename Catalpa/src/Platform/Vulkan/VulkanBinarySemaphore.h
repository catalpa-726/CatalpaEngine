#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

// 管理一个二进制信号量的所有权，供 Vulkan 后端使用
namespace Catalpa {

    class VulkanDevice;

    class VulkanBinarySemaphore {
    public:
        explicit VulkanBinarySemaphore(const VulkanDevice& device);
        ~VulkanBinarySemaphore();

        VulkanBinarySemaphore(const VulkanBinarySemaphore&) = delete;
        VulkanBinarySemaphore& operator=(const VulkanBinarySemaphore&) = delete;

        [[nodiscard]] RHIStatus Init();

        // 调用前，拥有者必须结束所有 GPU 和呈现操作对它的使用。
        void Shutdown();

        VkSemaphore GetHandle() const { return m_Semaphore; }

    private:
        const VulkanDevice& m_Device;
        VkSemaphore m_Semaphore = VK_NULL_HANDLE;
    };

}