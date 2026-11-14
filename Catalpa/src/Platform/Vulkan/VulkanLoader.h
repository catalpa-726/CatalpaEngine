#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

	class VulkanLoader {
	public:
		/**
		 * @brief 通过 GLAD 解析当前 Vulkan 实例、物理设备及逻辑设备的函数入口。
		 *
		 * @param[in] instance 本阶段已创建的原生实例。
		 * @param[in] physicalDevice 本阶段已选择的物理设备。
		 * @param[in] device 本阶段已创建的逻辑设备。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		static bool Load(VkInstance instance = VK_NULL_HANDLE, VkPhysicalDevice physicalDevice = VK_NULL_HANDLE,
		    VkDevice device = VK_NULL_HANDLE);

		static void Shutdown();
	};

}