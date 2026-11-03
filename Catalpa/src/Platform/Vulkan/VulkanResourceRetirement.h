#pragma once

#include "VulkanPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"

namespace Catalpa {

    class VulkanDevice;

    class VulkanResourceRetirement {
    public:
        explicit VulkanResourceRetirement(const VulkanDevice& device) : m_Device(device) {}

    private:
        friend class VulkanResourceRegistry;

        // 命令引用保持到安全重置或销毁；这里只接收已可清理且没有命令引用的管线。
        void RetirePipeline(VulkanPipelineRecord pipeline) noexcept;

        // 只接收已无命令引用、允许实际清理的 Buffer。
        void RetireBuffer(VulkanBufferRecord buffer) noexcept;

        // 只接收已经没有 View 引用的图像。
        void RetireTexture(VulkanTextureRecord texture) noexcept;

        // 只接收已经没有命令引用的 View；返回前完成实际销毁。
        void RetireTextureView(VulkanTextureViewRecord view) noexcept;

    private:
        const VulkanDevice& m_Device;
    };

}