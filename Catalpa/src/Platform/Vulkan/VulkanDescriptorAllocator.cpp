#include "catpch.h"
#include "VulkanDescriptorAllocator.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"
#include "RHI/RHIBindingDesc.h"

namespace Catalpa {

    VulkanDescriptorAllocator::~VulkanDescriptorAllocator() {
        Shutdown();
    }

    RHIStatus VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout layout,
                                                  std::span<const RHIBindingLayoutEntry> entries,
                                                  VkDescriptorSet& descriptorSet) {
        constexpr const char* operation = "VulkanDescriptorAllocator::Allocate";
        descriptorSet = VK_NULL_HANDLE;

        if (layout == VK_NULL_HANDLE || entries.size() > RHIMaxBindingsPerSet)
            return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
        if (m_AllocatedSets >= MaxSetsPerCommandList)
            return {RHIErrorCode::OutOfMemory, 0, "VulkanDescriptorAllocator/SetBudget"};
        std::array<uint32_t, 4> counts{};
        for (const auto& entry : entries) {
            switch (entry.type) {
            case RHIBindingType::StorageBuffer:
                ++counts[0];
                break;
            case RHIBindingType::SampledTexture:
                ++counts[1];
                break;
            case RHIBindingType::Sampler:
                ++counts[2];
                break;
            case RHIBindingType::UniformBuffer:
                ++counts[3];
                break;
            default:
                return {RHIErrorCode::InvalidDescription, 0, operation};
            }
        }

        const auto allocate = [&](VkDescriptorPool pool, VkDescriptorSet& allocated) {
            const VkDescriptorSetAllocateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                   .descriptorPool = pool,
                                                   .descriptorSetCount = 1,
                                                   .pSetLayouts = &layout};

            return vkAllocateDescriptorSets(m_Device.GetHandle(), &info, &allocated);
        };

        auto& currentPool = m_CurrentPools.try_emplace(counts, 0).first->second;
        for (; currentPool < m_Pools.size(); ++currentPool) {
            if (m_Pools[currentPool].counts != counts)
                continue;
            VkDescriptorSet allocated = VK_NULL_HANDLE;
            const VkResult result = allocate(m_Pools[currentPool].handle, allocated);

            if (result == VK_SUCCESS) {
                descriptorSet = allocated;
                ++m_AllocatedSets;
                return {};
            }

            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                return MakeVulkanStatus(result, "vkAllocateDescriptorSets");
        }

        if (m_Pools.size() >= MaxPools)
            return {.code = RHIErrorCode::OutOfMemory, .operation = operation};

        // 原生池创建前完成容量准备，发布时不再分配。
        if (m_Pools.size() == m_Pools.capacity())
            m_Pools.reserve(std::min(MaxPools, std::max(size_t(8), m_Pools.capacity() * 2)));
        constexpr std::array types{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                   VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        std::array<VkDescriptorPoolSize, 4> sizes{};
        uint32_t sizeCount = 0;
        for (size_t i = 0; i < counts.size(); ++i)
            if (counts[i])
                sizes[sizeCount++] = {types[i], SetsPerPool * counts[i]};

        const VkDescriptorPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .maxSets = SetsPerPool,
                                              .poolSizeCount = sizeCount,
                                              .pPoolSizes = sizes.data()};

        VkDescriptorPool pool = VK_NULL_HANDLE;
        const RHIStatus status = MakeVulkanStatus(
            vkCreateDescriptorPool(m_Device.GetHandle(), &info, nullptr, &pool), "vkCreateDescriptorPool");

        if (!status.IsSuccess())
            return status;

        VkDescriptorSet allocated = VK_NULL_HANDLE;
        const VkResult result = allocate(pool, allocated);

        if (result != VK_SUCCESS) {
            vkDestroyDescriptorPool(m_Device.GetHandle(), pool, nullptr);
            return MakeVulkanStatus(result, "vkAllocateDescriptorSets");
        }

        currentPool = m_Pools.size();
        m_Pools.push_back({pool, counts});
        descriptorSet = allocated;
        ++m_AllocatedSets;
        return {};
    }

    RHIStatus VulkanDescriptorAllocator::Reset()
    {
        for (const auto& pool : m_Pools) {
            const RHIStatus status = MakeVulkanStatus(
                vkResetDescriptorPool(m_Device.GetHandle(), pool.handle, 0), "vkResetDescriptorPool");

            if (!status.IsSuccess())
                return status;
        }

        for (auto& [counts, cursor] : m_CurrentPools)
            cursor = 0;
        m_AllocatedSets = 0;
        return {};
    }

    void VulkanDescriptorAllocator::Shutdown()
    {
        for (const auto& pool : m_Pools)
            vkDestroyDescriptorPool(m_Device.GetHandle(), pool.handle, nullptr);

        m_Pools.clear();
        m_CurrentPools.clear();
        m_AllocatedSets = 0;
    }

} // namespace Catalpa
