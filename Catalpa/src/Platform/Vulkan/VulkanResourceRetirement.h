#pragma once

#include "VulkanPipeline.h"

namespace Catalpa {

    class VulkanDevice;

    class VulkanResourceRetirement {
    public:
        explicit VulkanResourceRetirement(const VulkanDevice& device) : m_Device(device) {}

    private:
        friend class VulkanResourceRegistry;

        // 当前命令引用保持到安全重置，因此这里只接收已无命令引用的管线。
        void RetirePipeline(VulkanPipelineRecord pipeline) noexcept;

        const VulkanDevice& m_Device;
    };

}