#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

namespace Catalpa {

	/**
	 * @brief 将指针型或整数型的 Vulkan 句柄编码为调试 API 使用的对象值。
	 *
	 * @param[in] handle 待命名或标记的 Vulkan 原生对象句柄。
	 *
	 * @return 返回表示同一原生句柄的 64 位无符号数值，转换不改变对象的所有权。
	 */
	template <typename T> uint64_t VulkanHandleToUint64(T handle) noexcept {
		if constexpr (std::is_pointer_v<T>)
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
		else
			return static_cast<uint64_t>(handle);
	}

	/* VulkanDebugUtils 由实例持有，负责诊断消息、对象名称和命令标签，不管理渲染资源的所有权 */
	class VulkanDebugUtils {
	public:
		VulkanDebugUtils() = default;
		~VulkanDebugUtils();

		VulkanDebugUtils(const VulkanDebugUtils&) = delete;
		VulkanDebugUtils& operator=(const VulkanDebugUtils&) = delete;

		/**
		 * @brief 按已启用的调试扩展创建持久诊断信使，并设置日志详细程度。
		 * 仅在当前实例实际启用了 VK_EXT_debug_utils 时创建持久 messenger。
		 *
		 * @param[in] instance 已启用调试扩展的实例。
		 * @param[in] verboseMessages 是否同时记录详细级别的消息。
		 *
		 * @return 返回调试信使的初始化状态，可从失败状态定位缺失能力或创建错误。
		 */
		[[nodiscard]] RHIStatus Init(VkInstance instance, bool verboseMessages);

		/**
		 * @brief 销毁调试信使并解除其缓存的函数入口，供后续实例销毁。
		 * 在实例销毁前释放 messenger，并撤销缓存入口。
		 */
		void Shutdown();

		bool IsEnabled() const { return m_Enabled; }

		/**
		 * @brief 构造 Vulkan 调试信使的消息过滤条件和日志回调。
		 *
		 * @param[in] verboseMessages 是否在警告和错误以外接收信息及详细级别消息。
		 *
		 * @return 返回已填写严重程度、消息类别和回调入口的创建描述，调用者负责创建信使。
		 */
		static VkDebugUtilsMessengerCreateInfoEXT MakeMessengerCreateInfo(bool verboseMessages);

		/**
		 * @brief 为有效原生对象命名；命名失败不改变资源创建结果。
		 *
		 * @param[in] device 拥有对象的逻辑设备。
		 * @param[in] type 待命名对象的 Vulkan 类型。
		 * @param[in] handle 转换为 64 位整数的原生句柄。
		 * @param[in] name 以零结束的诊断名称。
		 */
		void NameObject(VkDevice device, VkObjectType type, uint64_t handle, const char* name) const;

		/**
		 * @brief 返回 true 表示实际发出了 Begin，调用方据此配对 End。
		 *
		 * @param[in] commands 接收调试标签的原生命令缓冲区。
		 * @param[in] name 以空字符结尾的 UTF-8 标签名称。
		 * @param[in] color 供调试工具显示的 RGBA 标签颜色。
		 *
		 * @return 已在命令缓冲区写入开始标签时返回 true，此时调用方须写入结束标签。
		 */
		[[nodiscard]] bool BeginLabel(
		    VkCommandBuffer commands, const char* name, const std::array<float, 4>& color) const;
		void EndLabel(VkCommandBuffer commands) const;

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
		bool m_Enabled = false;

		/* 缓存当前实例的入口，避免依赖后续 GLAD 加载修改的全局指针。 */
		PFN_vkDestroyDebugUtilsMessengerEXT m_DestroyMessenger = nullptr;
		PFN_vkSetDebugUtilsObjectNameEXT m_NameObject = nullptr;
		PFN_vkCmdBeginDebugUtilsLabelEXT m_BeginLabel = nullptr;
		PFN_vkCmdEndDebugUtilsLabelEXT m_EndLabel = nullptr;
	};

}