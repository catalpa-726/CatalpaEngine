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

        // 创建实例，并按请求配置验证层；失败保留具体操作与错误码。
        [[nodiscard]] RHIStatus Init(const std::vector<const char*>& extensions, const RHIDiagnosticsOptions& diagnostics, uint32_t apiVersion = VK_API_VERSION_1_3);

        // 借用当前实例持有的诊断入口。
        const VulkanDebugUtils& GetDebugUtils() const { return m_DebugUtils; }

        void Shutdown();

        VkInstance GetHandle() const { return m_Instance; }

        bool IsSurfaceMaintenanceEnabled() const { return m_SurfaceMaintenanceEnabled; }

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;

        bool m_SurfaceMaintenanceEnabled = false;

        VulkanDebugUtils m_DebugUtils;
    };

}