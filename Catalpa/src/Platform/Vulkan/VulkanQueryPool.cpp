#include "catpch.h"
#include "VulkanQueryPool.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {
    VulkanQueryPool::~VulkanQueryPool()
    {
        if (m_Handle != VK_NULL_HANDLE) vkDestroyQueryPool(m_Device.GetHandle(), m_Handle, nullptr);
    }

    DeviceId VulkanQueryPool::GetDeviceId() const noexcept { return m_Device.GetDeviceId(); }

    RHIStatus VulkanQueryPool::Init(const RHIQueryDesc& desc)
    {
        constexpr const char* operation = "VulkanQueryPool::Init";
        if (m_Handle != VK_NULL_HANDLE || desc.count == 0 || desc.count > 8192)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_Device.GetPhysicalDevice(), &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_Device.GetPhysicalDevice(), &count, families.data());
        const auto family = m_Device.GetGraphicsQueueFamily();
        if (family >= count || families[family].timestampValidBits == 0)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &properties);
        const VkQueryPoolCreateInfo info{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = desc.count };
        const auto status = MakeVulkanStatus(vkCreateQueryPool(m_Device.GetHandle(), &info, nullptr, &m_Handle), "vkCreateQueryPool");
        if (!status.IsSuccess()) return status;
        m_Count = desc.count;
        m_ValidBits = families[family].timestampValidBits;
        m_Period = properties.limits.timestampPeriod;
        return {};
    }

    RHIStatus VulkanQueryPool::GetResults(uint32_t first, std::span<RHIQueryResult> results)
    {
        if (m_Handle == VK_NULL_HANDLE || results.empty() || first >= m_Count || results.size() > m_Count - first)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanQueryPool::GetResults" };
        struct Result { uint64_t value = 0, available = 0; };
        std::vector<Result> native(results.size());
        const auto result = vkGetQueryPoolResults(m_Device.GetHandle(), m_Handle, first,
            static_cast<uint32_t>(results.size()), native.size() * sizeof(Result), native.data(), sizeof(Result),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result != VK_SUCCESS && result != VK_NOT_READY) return m_Device.ObserveStatus(MakeVulkanStatus(result, "vkGetQueryPoolResults"));
        for (size_t i = 0; i < results.size(); ++i) results[i] = { native[i].value, native[i].available != 0 };
        return {};
    }
}
