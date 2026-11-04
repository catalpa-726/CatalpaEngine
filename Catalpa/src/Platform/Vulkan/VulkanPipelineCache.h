#pragma once
#include "VulkanHeaders.h"
namespace Catalpa {
    // 一个VkDevice独占的native编译缓存；不拥有GraphicsPipeline或改变Registry退休规则。
    class VulkanPipelineCache {
    public:
        VulkanPipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties);
        ~VulkanPipelineCache();
        VulkanPipelineCache(const VulkanPipelineCache&) = delete;
        VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;
        [[nodiscard]] VkPipelineCache GetHandle() const noexcept { return m_Cache; }
    private:
        bool Matches(std::span<const std::byte>) const noexcept;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkPipelineCache m_Cache = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_Properties{};
        std::filesystem::path m_Path;
    };
}
