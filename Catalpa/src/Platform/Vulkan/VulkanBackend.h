#pragma once

#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanPhysicalDevice.h"

#include "RHI/RHIDevice.h"

namespace Catalpa {

	class Window;

	class VulkanBackend {
	public:
		VulkanBackend() = default;
		~VulkanBackend();

		VulkanBackend(const VulkanBackend&) = delete;
		VulkanBackend& operator=(const VulkanBackend&) = delete;

		/**
		 * @brief 初始化 Vulkan 实例与窗口表面并选择物理设备。
		 *
		 * @param[in] window 用于创建呈现表面的主窗口。
		 * @param[in] diagnostics 实例验证和消息配置。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init(Window& window, const RHIDiagnosticsOptions& diagnostics);
		/**
		 * @brief 在逻辑设备已收尾后销毁后端所持有的表面和实例。
		 */
		void Shutdown();

		const VulkanInstance& GetInstance() const { return m_Instance; }
		const VulkanPhysicalDevice& GetPhysicalDevice() const { return m_PhysicalDevice; }
		const VulkanSurface& GetSurface() const { return m_Surface; }

	private:
		/* 实例和表面自有，物理设备句柄为借用。 */
		VulkanInstance m_Instance;
		VulkanSurface m_Surface;
		VulkanPhysicalDevice m_PhysicalDevice;
		bool m_Started = false;
	};

	/**
	 * @brief 建立 Vulkan 后端、实例、窗口表面及逻辑设备，将成功创建的设备交给调用方。
	 * 集成 instance -> surface -> physical device 的创建，最后创建 device
	 *
	 * @param[in] window 提供原生窗口，后端据此创建用于呈现的表面。
	 * @param[in] desc 后端创建选项，此处使用其中的 Vulkan 诊断配置。
	 * @param[out] status 输出创建结果；失败时记录后端初始化错误并返回空指针。
	 *
	 * @return 成功返回拥有逻辑设备的指针，初始化失败返回空指针并写入 status。
	 */
	std::unique_ptr<RHIDevice> CreateVulkanDevice(Window& window, const RHIBackendDesc& desc, RHIStatus& status);

}