#include "catpch.h"
#include "VulkanDescriptorAllocator.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"
#include "RHI/RHIBindingDesc.h"

namespace Catalpa {

    VulkanDescriptorAllocator::~VulkanDescriptorAllocator() { Shutdown(); }

    RHIStatus VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout layout, VkDescriptorSet& descriptorSet)
    {
        constexpr const char* operation = "VulkanDescriptorAllocator::Allocate";
        descriptorSet = VK_NULL_HANDLE;

        if (layout == VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto allocate = [&](VkDescriptorPool pool, VkDescriptorSet& allocated) {
            const VkDescriptorSetAllocateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layout
            };

            return vkAllocateDescriptorSets(m_Device.GetHandle(), &info, &allocated);
        };

        for (; m_CurrentPool < m_Pools.size(); ++m_CurrentPool) {
            VkDescriptorSet allocated = VK_NULL_HANDLE;
            const VkResult result = allocate(m_Pools[m_CurrentPool], allocated);

            if (result == VK_SUCCESS) {
                descriptorSet = allocated;
                return {};
            }

            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                return MakeVulkanStatus(result, "vkAllocateDescriptorSets");
        }

        if (m_Pools.size() == m_Pools.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        // 原生池创建前完成容量准备，发布时不再分配。
        m_Pools.reserve(m_Pools.size() + 1);

        const std::array sizes{
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = SetsPerPool * RHIMaxBindingsPerSet
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = SetsPerPool * RHIMaxBindingsPerSet
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = SetsPerPool * RHIMaxBindingsPerSet
            }
        };

        const VkDescriptorPoolCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = SetsPerPool,
            .poolSizeCount = static_cast<uint32_t>(sizes.size()),
            .pPoolSizes = sizes.data()
        };

        VkDescriptorPool pool = VK_NULL_HANDLE;
        const RHIStatus status = MakeVulkanStatus(
            vkCreateDescriptorPool(m_Device.GetHandle(), &info, nullptr, &pool),
            "vkCreateDescriptorPool");

        if (!status.IsSuccess()) return status;

        VkDescriptorSet allocated = VK_NULL_HANDLE;
        const VkResult result = allocate(pool, allocated);

        if (result != VK_SUCCESS) {
            vkDestroyDescriptorPool(m_Device.GetHandle(), pool, nullptr);
            return MakeVulkanStatus(result, "vkAllocateDescriptorSets");
        }

        m_Pools.push_back(pool);
        descriptorSet = allocated;
        return {};
    }

    RHIStatus VulkanDescriptorAllocator::Reset()
    {
        for (const VkDescriptorPool pool : m_Pools) {
            const RHIStatus status = MakeVulkanStatus(
                vkResetDescriptorPool(m_Device.GetHandle(), pool, 0),
                "vkResetDescriptorPool");

            if (!status.IsSuccess()) return status;
        }

        m_CurrentPool = 0;
        return {};
    }

    void VulkanDescriptorAllocator::Shutdown()
    {
        for (const VkDescriptorPool pool : m_Pools)
            vkDestroyDescriptorPool(m_Device.GetHandle(), pool, nullptr);

        m_Pools.clear();
        m_CurrentPool = 0;
    }

}
