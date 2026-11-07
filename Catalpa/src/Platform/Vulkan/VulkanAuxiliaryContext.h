#pragma once
#include "VulkanCommandPool.h"
#include "VulkanCommandList.h"
#include "VulkanFence.h"

namespace Catalpa {
    // 每个batch独占command/descriptor池；只在该batch的真实Fence完成后重置。
    class VulkanAuxiliaryContext final {
      public:
        VulkanAuxiliaryContext(VulkanDevice&, QueueId);
        ~VulkanAuxiliaryContext();
        RHIStatus Init(uint32_t family);
        RHIStatus PrepareReuse(uint64_t timeout, bool& ready);
        VulkanCommandList& Commands() noexcept {
            return m_Commands;
        }
        VulkanFence& Fence() noexcept {
            return m_Fence;
        }
        QueueId Queue() const noexcept {
            return m_Queue;
        }
        uint64_t generation = 0;

      private:
        VulkanDevice& m_Device;
        QueueId m_Queue;
        VulkanFence m_Fence;
        VulkanCommandPool m_Pool;
        VulkanCommandList m_Commands;
    };
} // namespace Catalpa
