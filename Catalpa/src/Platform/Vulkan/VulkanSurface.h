#pragma once

#include "VulkanHeaders.h"

namespace Catalpa {

	class Window;

	class VulkanSurface {
	public:
		VulkanSurface() = default;
		~VulkanSurface();

		VulkanSurface(const VulkanSurface&) = delete;
		VulkanSurface& operator=(const VulkanSurface&) = delete;

		/**
		 * @brief 返回必需扩展集合。
		 *
		 * @param[in] extensions 扩展集合。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		static bool GetRequiredExtensions(std::vector<const char*>& extensions);

		bool Init(VkInstance instance, Window& window);
		/**
		 * @brief 为外部 GLFW 窗口创建与指定 Vulkan 实例关联的呈现表面。
		 *
		 * @param[in] instance 创建并最终销毁该表面的 Vulkan 实例。
		 * @param[in] nativeWindow 实际类型为 GLFWwindow* 的窗口指针，调用时须有效。
		 *
		 * @return 原生表面成功创建并登记时返回 true；窗口为空或创建失败时返回 false。
		 */
		bool InitNative(VkInstance instance, void* nativeWindow);
		/**
		 * @brief 销毁窗口呈现表面并清除与实例的关联。
		 */
		void Shutdown();

		VkSurfaceKHR GetHandle() const { return m_Surface; }

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	};

}