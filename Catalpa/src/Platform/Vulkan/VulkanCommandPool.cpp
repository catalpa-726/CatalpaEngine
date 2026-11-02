#include "catpch.h"
#include "VulkanCommandPool.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

    VulkanCommandPool::VulkanCommandPool(const VulkanDevice& device) : m_Device(device) {}

    VulkanCommandPool::~VulkanCommandPool() { Shutdown(); }

    // 	为指定队列族创建命令池
    RHIStatus VulkanCommandPool::Init(uint32_t queueFamily)
    {
        CAT_CORE_ASSERT(m_Pool == VK_NULL_HANDLE, "Vulkan command pool already exists!");

        const VkCommandPoolCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = queueFamily
        };

        VkCommandPool pool = VK_NULL_HANDLE;
        const RHIStatus status = MakeVulkanStatus(vkCreateCommandPool(m_Device.GetHandle(), &createInfo, nullptr, &pool), "vkCreateCommandPool");
        if (!status.IsSuccess()) return status;

        m_Pool = pool;
        m_QueueFamily = queueFamily;
        return {};
    }

    // 分配一个主命令缓冲
    RHIStatus VulkanCommandPool::AllocatePrimary(VkCommandBuffer& commandBuffer)
    {
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_Pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        return MakeVulkanStatus(vkAllocateCommandBuffers(m_Device.GetHandle(), &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
    }

    // 将池内命令缓冲恢复到可重新录制的初始状态
    RHIStatus VulkanCommandPool::Reset() { return MakeVulkanStatus(vkResetCommandPool(m_Device.GetHandle(), m_Pool, 0), "vkResetCommandPool"); }

    void VulkanCommandPool::Shutdown()
    {
        if (m_Pool != VK_NULL_HANDLE) vkDestroyCommandPool(m_Device.GetHandle(), m_Pool, nullptr);
        m_Pool = VK_NULL_HANDLE;
        m_QueueFamily = VK_QUEUE_FAMILY_IGNORED;
    }

}