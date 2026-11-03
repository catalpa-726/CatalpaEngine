#include "catpch.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanConversions.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanBuffer(VulkanDevice& device, const RHIBufferDesc& desc, VulkanBufferRecord& record)
    {
        constexpr const char* operation = "CreateVulkanBuffer";
        record = {};

        const VkBufferUsageFlags usage = ToVulkanBufferUsage(desc.usage);

        if (desc.size == 0 || usage == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        VmaAllocationCreateInfo allocationInfo{};

        switch (desc.memory) {
        case RHIMemoryDomain::Device:
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case RHIMemoryDomain::Upload:
            if (desc.size > std::numeric_limits<size_t>::max())
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        default:
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        const VkBufferCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationInfo allocated{};
        const VmaAllocator allocator = device.GetMemoryAllocator().GetHandle();
        const RHIStatus status = MakeVulkanStatus(
            vmaCreateBuffer(allocator, &info, &allocationInfo, &record.buffer, &record.allocation, &allocated),
            "vmaCreateBuffer");

        if (!status.IsSuccess()) return status;

        record.desc = desc;
        record.mapped = static_cast<std::byte*>(allocated.pMappedData);

        if (desc.memory == RHIMemoryDomain::Upload && !record.mapped) {
            DestroyVulkanBuffer(device, record);
            return { .code = RHIErrorCode::BackendFailure, .operation = operation };
        }

        return {};
    }

    RHIStatus WriteVulkanBuffer(const VulkanDevice& device, const VulkanBufferRecord& record, uint64_t offset, std::span<const std::byte> data)
    {
        constexpr const char* operation = "WriteVulkanBuffer";

        if (record.desc.memory != RHIMemoryDomain::Upload || !record.mapped)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (offset > record.desc.size || data.size() > record.desc.size - offset)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (data.empty()) return {};

        std::memcpy(record.mapped + static_cast<size_t>(offset), data.data(), data.size());

        // offset 相对于 allocation 内容，不叠加底层 VkDeviceMemory 偏移。
        return MakeVulkanStatus(
            vmaFlushAllocation(device.GetMemoryAllocator().GetHandle(), record.allocation, offset, data.size()),
            "vmaFlushAllocation");
    }

    void DestroyVulkanBuffer(const VulkanDevice& device, VulkanBufferRecord& record) noexcept
    {
        if (record.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(device.GetMemoryAllocator().GetHandle(), record.buffer, record.allocation);

        record = {};
    }

}