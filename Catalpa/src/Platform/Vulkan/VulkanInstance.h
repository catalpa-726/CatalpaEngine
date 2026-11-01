#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

    class VulkanInstance {
    public:
        VulkanInstance() = default;
        ~VulkanInstance();

        VulkanInstance(const VulkanInstance&) = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;

        bool Init(const std::vector<const char*>& extensions, uint32_t apiVersion = VK_API_VERSION_1_3);

        void Shutdown();

        VkInstance GetHandle() const { return m_Instance; }

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
    };

}