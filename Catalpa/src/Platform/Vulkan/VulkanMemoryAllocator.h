#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "RHI/RHIMemoryBudget.h"

#include <vk_mem_alloc.h>

namespace Catalpa {

	class VulkanMemoryAllocator {
	public:
		VulkanMemoryAllocator() = default;
		~VulkanMemoryAllocator();

		VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
		VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;

		/**
		 * @brief 用现有 Vulkan 实例、物理设备和逻辑设备初始化 GPU 内存分配器。
		 * 在逻辑设备和 GLAD 函数加载完成后初始化。
		 *
		 * @param[in] instance 当前 Vulkan 实例。
		 * @param[in] physicalDevice 提供内存类型和堆信息的物理设备。
		 * @param[in] device 分配内存的逻辑设备。
		 * @param[in] memoryBudgetExtension 是否已启用可查询预算的扩展。
		 *
		 * @return 返回 Vulkan 内存分配器的初始化状态。
		 */
		[[nodiscard]] RHIStatus Init(
		    VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, bool memoryBudgetExtension = false);
		/**
		 * @brief 返回预算。
		 *
		 * @param[out] output 接收 std::vector<RHIMemoryHeapBudget> 结果。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus GetBudgets(std::vector<RHIMemoryHeapBudget>&) const;

		/**
		 * @brief 释放 GPU 内存分配器；调用前须归还它创建的全部分配。
		 * 调用前须释放全部内存分配，本函数不会等待 GPU。
		 */
		void Shutdown();

		VmaAllocator GetHandle() const { return m_Allocator; }

	private:
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
		bool m_DriverBudget = false;
	};

} /* 命名空间 Catalpa */
