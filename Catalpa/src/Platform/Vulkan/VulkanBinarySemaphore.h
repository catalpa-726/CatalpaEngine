#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

/* 管理一个二进制信号量的所有权，供 Vulkan 后端使用 */
namespace Catalpa {

	class VulkanDevice;

	class VulkanBinarySemaphore {
	public:
		explicit VulkanBinarySemaphore(const VulkanDevice& device);
		~VulkanBinarySemaphore();

		VulkanBinarySemaphore(const VulkanBinarySemaphore&) = delete;
		VulkanBinarySemaphore& operator=(const VulkanBinarySemaphore&) = delete;

		/**
		 * @brief 创建用于队列或交换链同步的二值信号量。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init();

		/**
		 * @brief 销毁二值信号量，调用前须确认此前使用它的 GPU 和呈现操作已结束。
		 * 调用前，拥有者必须结束所有 GPU 和呈现操作对它的使用。
		 */
		void Shutdown();

		VkSemaphore GetHandle() const { return m_Semaphore; }

	private:
		const VulkanDevice& m_Device;
		VkSemaphore m_Semaphore = VK_NULL_HANDLE;
	};

}