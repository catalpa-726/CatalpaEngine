#pragma once

#include "VulkanHeaders.h"
#include "VulkanDebugUtils.h"
#include "RHI/RHIBackend.h"
#include "RHI/RHIError.h"

namespace Catalpa {

	class VulkanInstance {
	public:
		VulkanInstance() = default;
		~VulkanInstance();

		VulkanInstance(const VulkanInstance&) = delete;
		VulkanInstance& operator=(const VulkanInstance&) = delete;

		/**
		 * @brief 集中处理扩展和 layer 的选择，并查询验证层自身提供的扩展
		 * 创建实例，并按请求配置验证层；失败保留具体操作与错误码。
		 *
		 * @param[in] extensions 调用方所需的实例扩展名称。
		 * @param[in] diagnostics 实例验证和日志选项。
		 * @param[in] apiVersion 请求创建的 Vulkan API 版本。
		 *
		 * @return 返回实例、所请求扩展及诊断配置的初始化状态。
		 */
		[[nodiscard]] RHIStatus Init(const std::vector<const char*>& extensions,
		    const RHIDiagnosticsOptions& diagnostics, uint32_t apiVersion = VK_API_VERSION_1_3);

		const VulkanDebugUtils& GetDebugUtils() const { return m_DebugUtils; }

		/**
		 * @brief 先释放调试回调，再销毁原生实例。
		 */
		void Shutdown();

		VkInstance GetHandle() const { return m_Instance; }

		bool IsSurfaceMaintenanceEnabled() const { return m_SurfaceMaintenanceEnabled; }

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;

		bool m_SurfaceMaintenanceEnabled = false;

		VulkanDebugUtils m_DebugUtils;
	};

}