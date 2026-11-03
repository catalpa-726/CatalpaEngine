#pragma once

#include "VulkanMemoryAllocator.h"
#include "RHI/RHIBufferDesc.h"

// 定义后端 Buffer 记录及创建、写入、销毁接口
namespace Catalpa {

    class VulkanDevice;

    // Registry 独占管理原生载荷；记录本身不自动销毁。
    struct VulkanBufferRecord {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RHIBufferDesc desc{};
        std::byte* mapped = nullptr;
    };

    [[nodiscard]] RHIStatus CreateVulkanBuffer(VulkanDevice& device, const RHIBufferDesc& desc, VulkanBufferRecord& record);
    [[nodiscard]] RHIStatus WriteVulkanBuffer(const VulkanDevice& device, const VulkanBufferRecord& record, uint64_t offset, std::span<const std::byte> data);

    // 仅用于尚未发布的失败记录，或退休系统已确认可销毁的记录。
    void DestroyVulkanBuffer(const VulkanDevice& device, VulkanBufferRecord& record) noexcept;

}