#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

	class VulkanPhysicalDevice {
	public:
		/**
		 * @brief 选择满足渲染与窗口呈现要求的物理设备及其队列族。
		 *
		 * @param[in] instance 用于枚举适配器的实例。
		 * @param[in] surface 要支持呈现的窗口表面。
		 * @param[in] surfaceMaintenanceEnabled 实例是否启用了表面维护扩展。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool Init(VkInstance instance, VkSurfaceKHR surface, bool surfaceMaintenanceEnabled);

		VkPhysicalDevice GetHandle() const { return m_PhysicalDevice; }
		const VkPhysicalDeviceProperties& GetProperties() const { return m_Properties; }
		uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
		uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

		bool SupportsSwapchainMaintenance() const { return m_SwapchainMaintenanceSupported; }

	private:
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties m_Properties{};
		uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

		bool m_SwapchainMaintenanceSupported = false;
	};

}