#include "catpch.h"
#include "VulkanDebugUtils.h"
#include "VulkanUtils.h"
#include "Log/Log.h"

namespace Catalpa {

    // 保留消息类型、ID、原文及命令标签上下文；只观察，不改变 Vulkan 操作结果。
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*) noexcept
    {
        const char* id = data && data->pMessageIdName ? data->pMessageIdName : "unknown";
        const char* message = data && data->pMessage ? data->pMessage : "(no message)";
        const int32_t number = data ? data->messageIdNumber : 0;

        std::string labels;

        if (data) {
            for (uint32_t i = 0; i < data->cmdBufLabelCount; ++i) {
                const char* name = data->pCmdBufLabels[i].pLabelName;
                if (!name) continue;
                if (!labels.empty()) labels += " / ";
                labels += name;
            }
        }

        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            CAT_CORE_ERROR("Vulkan [type={0}, id={1} ({2}), labels={3}] {4}", types, id, number, labels, message);
        else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            CAT_CORE_WARN("Vulkan [type={0}, id={1} ({2}), labels={3}] {4}", types, id, number, labels, message);
        else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
            CAT_CORE_INFO("Vulkan [type={0}, id={1} ({2}), labels={3}] {4}", types, id, number, labels, message);
        else
            CAT_CORE_TRACE("Vulkan [type={0}, id={1} ({2}), labels={3}] {4}", types, id, number, labels, message);

        return VK_FALSE;
    }

    VulkanDebugUtils::~VulkanDebugUtils() { Shutdown(); }

    VkDebugUtilsMessengerCreateInfoEXT VulkanDebugUtils::MakeMessengerCreateInfo(bool verboseMessages)
    {
        VkDebugUtilsMessageSeverityFlagsEXT severity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

        if (verboseMessages) severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

        return {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = severity,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = DebugMessageCallback,
            .pUserData = nullptr
        };
    }

    RHIStatus VulkanDebugUtils::Init(VkInstance instance, bool verboseMessages)
    {
        constexpr const char* operation = "VulkanDebugUtils::Init";

        if (m_Instance != VK_NULL_HANDLE || m_Enabled)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (instance == VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto getProc = vkGetInstanceProcAddr;

        if (!getProc)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(getProc(instance, "vkCreateDebugUtilsMessengerEXT"));
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(getProc(instance, "vkDestroyDebugUtilsMessengerEXT"));
        const auto nameObject = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(getProc(instance, "vkSetDebugUtilsObjectNameEXT"));
        const auto beginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(getProc(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        const auto endLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(getProc(instance, "vkCmdEndDebugUtilsLabelEXT"));

        if (!createMessenger || !destroyMessenger || !nameObject || !beginLabel || !endLabel) {
            CAT_CORE_ERROR("Required Vulkan debug utils entry points are unavailable.");
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        }

        const VkDebugUtilsMessengerCreateInfoEXT info = MakeMessengerCreateInfo(verboseMessages);
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

        const RHIStatus status = MakeVulkanStatus(createMessenger(instance, &info, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
        if (!status.IsSuccess()) return status;

        // 全部创建成功后再发布成员状态。
        m_Instance = instance;
        m_Messenger = messenger;
        m_DestroyMessenger = destroyMessenger;
        m_NameObject = nameObject;
        m_BeginLabel = beginLabel;
        m_EndLabel = endLabel;
        m_Enabled = true;
        return {};
    }

    void VulkanDebugUtils::Shutdown()
    {
        m_Enabled = false;

        if (m_Messenger != VK_NULL_HANDLE) m_DestroyMessenger(m_Instance, m_Messenger, nullptr);

        m_Messenger = VK_NULL_HANDLE;
        m_Instance = VK_NULL_HANDLE;
        m_DestroyMessenger = nullptr;
        m_NameObject = nullptr;
        m_BeginLabel = nullptr;
        m_EndLabel = nullptr;
    }

    void VulkanDebugUtils::NameObject(VkDevice device, VkObjectType type, uint64_t handle, const char* name) const
    {
        if (!m_Enabled) return;

        if (device == VK_NULL_HANDLE || handle == 0 || type == VK_OBJECT_TYPE_UNKNOWN) {
            CAT_CORE_WARN("Cannot name Vulkan object: invalid device, handle or object type.");
            return;
        }

        const VkDebugUtilsObjectNameInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = type,
            .objectHandle = handle,
            .pObjectName = name
        };

        const VkResult result = m_NameObject(device, &info);

        if (result != VK_SUCCESS)
            CAT_CORE_WARN("Failed to name Vulkan object '{0}'. VkResult: {1}", name ? name : "(unnamed)", static_cast<int>(result));
    }

    bool VulkanDebugUtils::BeginLabel(VkCommandBuffer commands, const char* name, const std::array<float, 4>& color) const
    {
        if (!m_Enabled || commands == VK_NULL_HANDLE || !name || !m_BeginLabel || !m_EndLabel) return false;

        const VkDebugUtilsLabelEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = name,
            .color = { color[0], color[1], color[2], color[3] }
        };

        m_BeginLabel(commands, &info);
        return true;
    }

    void VulkanDebugUtils::EndLabel(VkCommandBuffer commands) const
    {
        if (m_Enabled && commands != VK_NULL_HANDLE && m_EndLabel) m_EndLabel(commands);
    }

}