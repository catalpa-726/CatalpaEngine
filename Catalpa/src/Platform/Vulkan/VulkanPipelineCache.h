#pragma once
#include "VulkanHeaders.h"
namespace Catalpa {
	/* 此原生管线编译缓存由一个 VkDevice 独占。图形管线及其回收仍由注册表管理。 */
	class VulkanPipelineCache {
	public:
		/**
		 * @brief 为逻辑设备创建管线缓存，已有缓存文件须通过设备标识和摘要校验后才可复用。
		 *
		 * @param[in] device 创建、使用和销毁该管线缓存的原生逻辑设备，须在缓存对象存活期间有效。
		 * @param[in] properties 物理设备的厂商、设备、驱动版本及缓存 UUID，用于选择和校验本机的缓存文件。
		 */
		VulkanPipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties);
		/**
		 * @brief 结束 VulkanPipelineCache 对象的生命周期。
		 */
		~VulkanPipelineCache();
		VulkanPipelineCache(const VulkanPipelineCache&) = delete;
		VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;
		[[nodiscard]] VkPipelineCache GetHandle() const noexcept { return m_Cache; }

	private:
		bool Matches(std::span<const std::byte>) const noexcept;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineCache m_Cache = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties m_Properties{};
		std::filesystem::path m_Path;
	};
}
