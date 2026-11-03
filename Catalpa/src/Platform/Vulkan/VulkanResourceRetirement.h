#pragma once

#include "VulkanPipeline.h"

namespace Catalpa {

    class VulkanDevice;

    class VulkanResourceRetirement {
    public:
        explicit VulkanResourceRetirement(const VulkanDevice& device) : m_Device(device) {}

    private:
        friend class VulkanResourceRegistry;

        // 命令引用保持到安全重置或销毁；这里只接收已可清理且没有命令引用的管线。
        void RetirePipeline(VulkanPipelineRecord pipeline) noexcept;

        const VulkanDevice& m_Device;
    };

}