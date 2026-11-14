#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

/* 管理属于指定队列族的命令池，提供主命令缓冲分配和整池重置 */
namespace Catalpa {

	class VulkanDevice;

	class VulkanCommandPool {
	public:
		explicit VulkanCommandPool(const VulkanDevice& device);
		~VulkanCommandPool();

		VulkanCommandPool(const VulkanCommandPool&) = delete;
		VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;

		/**
		 * @brief 为给定队列族创建 Vulkan 命令池。
		 *
		 * @param[in] queueFamily 使用该池录制命令的队列族索引。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init(uint32_t queueFamily);
		/**
		 * @brief 从命令池分配一个主命令缓冲区。
		 *
		 * @param[out] commandBuffer 接收新命令缓冲区的原生句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus AllocatePrimary(VkCommandBuffer& commandBuffer);

		/**
		 * @brief 重置前须停止录制，并确认 GPU 已结束对池内命令的使用；未提交的命令可以丢弃。
		 *
		 * @return 返回 Vulkan 命令池重置结果。
		 */
		[[nodiscard]] RHIStatus Reset();
		/**
		 * @brief 销毁整个原生命令池并解除其队列族关联。
		 */
		void Shutdown();

		VkCommandPool GetHandle() const { return m_Pool; }
		uint32_t GetQueueFamily() const { return m_QueueFamily; }

	private:
		const VulkanDevice& m_Device;
		VkCommandPool m_Pool = VK_NULL_HANDLE;
		uint32_t m_QueueFamily = VK_QUEUE_FAMILY_IGNORED;
	};

}