#pragma once

#include "VulkanMemoryAllocator.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHIHandle.h"

// 保存原生图像、VMA allocation、View 及父图像关系；生命周期由 Registry 管理
namespace Catalpa {

    class VulkanDevice;

    // 借用图像不接管原生对象及其内存。
    struct VulkanTextureRecord {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RHITextureDesc desc{};
        bool borrowed = false;
        bool presentable = false;
    };

    // View 保留父纹理，命令引用则保留 View。
    struct VulkanTextureViewRecord {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        Extent2D extent{};
        PixelFormat format = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
        RHITextureUsage usage = RHITextureUsage::None;
        bool presentable = false;
        bool linearFiltering = false;
        TextureHandle texture{};
        bool borrowed = false;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        RHITextureViewType type = RHITextureViewType::Texture2D;
    };

    // 检查创建参数和设备支持，再通过 VMA 创建图像。
    [[nodiscard]] RHIStatus CreateVulkanTexture(
        VulkanDevice& device, const RHITextureDesc& desc, VulkanTextureRecord& record);

    // Registry 已验证父句柄；这里检查 View 参数并创建原生视图。
    [[nodiscard]] RHIStatus CreateVulkanTextureView(
        VulkanDevice& device, TextureHandle texture, const VulkanTextureRecord& parent,
        const RHITextureViewDesc& desc, VulkanTextureViewRecord& record);

    // 仅接收未发布或已满足退休条件的记录；借用记录只清空自身。
    void DestroyVulkanTexture(const VulkanDevice& device, VulkanTextureRecord& record) noexcept;
    void DestroyVulkanTextureView(const VulkanDevice& device, VulkanTextureViewRecord& record) noexcept;

}
