#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    // 将不同表示形式的原生句柄转换为 debug utils 使用的整数值。
    template<typename T>
    uint64_t VulkanHandleToUint64(T handle) noexcept
    {
        if constexpr (std::is_pointer_v<T>) return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
        else return static_cast<uint64_t>(handle);
    }

    // VulkanDebugUtils 由实例持有，负责诊断消息、对象名称和命令标签，不管理渲染资源的所有权
    class VulkanDebugUtils {
    public:
        VulkanDebugUtils() = default;
        ~VulkanDebugUtils();

        VulkanDebugUtils(const VulkanDebugUtils&) = delete;
        VulkanDebugUtils& operator=(const VulkanDebugUtils&) = delete;

        // 仅在当前实例实际启用了 VK_EXT_debug_utils 时创建持久 messenger。
        [[nodiscard]] RHIStatus Init(VkInstance instance, bool verboseMessages);

        // 在实例销毁前释放 messenger，并撤销缓存入口。
        void Shutdown();

        bool IsEnabled() const { return m_Enabled; }

        // 构造静态回调配置，也用于捕获实例创建和销毁期间的消息。
        static VkDebugUtilsMessengerCreateInfoEXT MakeMessengerCreateInfo(bool verboseMessages);

        // 为有效原生对象命名；命名失败不改变资源创建结果。
        void NameObject(VkDevice device, VkObjectType type, uint64_t handle, const char* name) const;

        // 返回 true 表示实际发出了 Begin，调用方据此配对 End。
        [[nodiscard]] bool BeginLabel(VkCommandBuffer commands, const char* name, const std::array<float, 4>& color) const;
        void EndLabel(VkCommandBuffer commands) const;

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
        bool m_Enabled = false;

        // 缓存当前实例的入口，避免依赖后续 GLAD 加载修改的全局指针。
        PFN_vkDestroyDebugUtilsMessengerEXT m_DestroyMessenger = nullptr;
        PFN_vkSetDebugUtilsObjectNameEXT m_NameObject = nullptr;
        PFN_vkCmdBeginDebugUtilsLabelEXT m_BeginLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT m_EndLabel = nullptr;
    };

}