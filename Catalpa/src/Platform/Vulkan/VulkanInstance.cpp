#include "catpch.h"
#include "Log/Log.h"
#include "VulkanInstance.h"
#include "VulkanLoader.h"
#include "VulkanUtils.h"

namespace Catalpa {

    VulkanInstance::~VulkanInstance() { Shutdown(); }

    // 集中处理扩展和 layer 的选择，并查询验证层自身提供的扩展
    RHIStatus VulkanInstance::Init(const std::vector<const char*>& extensions, const RHIDiagnosticsOptions& diagnostics, uint32_t apiVersion)
    {
        constexpr const char* operation = "VulkanInstance::Init";
        constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";

        if (m_Instance != VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (diagnostics.validation != RHIValidationMode::Disabled &&
            diagnostics.validation != RHIValidationMode::Core &&
            diagnostics.validation != RHIValidationMode::Synchronization)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (!VulkanLoader::Load())
            return { .code = RHIErrorCode::BackendFailure, .operation = "VulkanLoader::Load" };

        const bool validationEnabled = diagnostics.validation != RHIValidationMode::Disabled;
        const bool synchronizationRequested = diagnostics.validation == RHIValidationMode::Synchronization;

        std::vector<VkExtensionProperties> availableExtensions;

        VkResult result = EnumerateVulkan(availableExtensions, [](uint32_t* count, VkExtensionProperties* data) {
            return vkEnumerateInstanceExtensionProperties(nullptr, count, data);
            });

        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkEnumerateInstanceExtensionProperties");

        std::vector<const char*> enabledLayers;

        if (validationEnabled) {
            std::vector<VkLayerProperties> layers;

            result = EnumerateVulkan(layers, [](uint32_t* count, VkLayerProperties* data) {
                return vkEnumerateInstanceLayerProperties(count, data);
                });

            if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkEnumerateInstanceLayerProperties");

            const bool found = std::ranges::any_of(layers, [&](const auto& layer) {
                return std::strcmp(layer.layerName, validationLayer) == 0;
                });

            if (!found) {
                CAT_CORE_ERROR("Requested Vulkan validation layer is unavailable: {0}", validationLayer);
                return { .code = RHIErrorCode::Unsupported, .operation = "VulkanInstance::ValidationLayer" };
            }

            // layer_settings 可能由显式 layer 提供，不能只看全局扩展或 GLAD flag。
            std::vector<VkExtensionProperties> layerExtensions;

            result = EnumerateVulkan(layerExtensions, [&](uint32_t* count, VkExtensionProperties* data) {
                return vkEnumerateInstanceExtensionProperties(validationLayer, count, data);
                });

            if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkEnumerateInstanceExtensionProperties");

            availableExtensions.insert(availableExtensions.end(), layerExtensions.begin(), layerExtensions.end());
            enabledLayers.push_back(validationLayer);
        }

        const auto hasExtension = [&](const char* name) {
            return std::ranges::any_of(availableExtensions, [&](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
                });
            };

        std::vector<const char*> enabledExtensions;

        // 相同扩展只登记一次。
        const auto addExtension = [&](const char* name) {
            if (std::ranges::none_of(enabledExtensions, [&](const char* existing) { return std::strcmp(existing, name) == 0; }))
                enabledExtensions.push_back(name);
            };

        for (const char* name : extensions) {
            if (!name || !hasExtension(name)) {
                CAT_CORE_ERROR("Required Vulkan instance extension is unavailable: {0}", name ? name : "(null)");
                return { .code = RHIErrorCode::Unsupported, .operation = "VulkanInstance::RequiredExtension" };
            }

            addExtension(name);
        }

        const bool surfaceMaintenanceEnabled =
            hasExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
            hasExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);

        if (surfaceMaintenanceEnabled) {
            addExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            addExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }

        if (validationEnabled) {
            for (const char* name : { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME }) {
                if (!hasExtension(name)) {
                    CAT_CORE_ERROR("Required Vulkan diagnostics extension is unavailable: {0}", name);
                    return { .code = RHIErrorCode::Unsupported, .operation = "VulkanInstance::DiagnosticsExtension" };
                }

                addExtension(name);
            }
        }

        const VkBool32 enabled = VK_TRUE;
        const VkBool32 enableSynchronization = synchronizationRequested ? VK_TRUE : VK_FALSE;

        const std::array<VkLayerSettingEXT, 3> settings{ {
            { validationLayer, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enabled },
            { validationLayer, "thread_safety", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enabled },
            { validationLayer, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enableSynchronization }
        } };

        const VkDebugUtilsMessengerCreateInfoEXT debugInfo = VulkanDebugUtils::MakeMessengerCreateInfo(diagnostics.verboseMessages);

        const VkLayerSettingsCreateInfoEXT layerSettings{
            .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            .pNext = &debugInfo,
            .settingCount = static_cast<uint32_t>(settings.size()),
            .pSettings = settings.data()
        };

        const VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "CatalpaEngine",
            .pEngineName = "CatalpaEngine",
            .apiVersion = apiVersion
        };

        const VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = validationEnabled ? &layerSettings : nullptr,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
            .ppEnabledLayerNames = enabledLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
            .ppEnabledExtensionNames = enabledExtensions.data()
        };

        // 保存已加载的查询入口，供后续实例函数加载失败时清理。
        const auto getInstanceProcAddr = vkGetInstanceProcAddr;
        VkInstance instance = VK_NULL_HANDLE;

        result = vkCreateInstance(&createInfo, nullptr, &instance);
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkCreateInstance");

        const auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProcAddr(instance, "vkDestroyInstance"));

        if (!VulkanLoader::Load(instance)) {
            destroyInstance(instance, nullptr);
            return { .code = RHIErrorCode::BackendFailure, .operation = "VulkanLoader::Load" };
        }

        m_Instance = instance;
        m_SurfaceMaintenanceEnabled = surfaceMaintenanceEnabled;

        if (validationEnabled) {
            const RHIStatus status = m_DebugUtils.Init(instance, diagnostics.verboseMessages);

            if (!status.IsSuccess()) {
                Shutdown();
                return status;
            }
        }

        CAT_CORE_INFO("[VulkanInstance] Vulkan diagnostics configured: {0}.",
            synchronizationRequested ? "core and synchronization validation" :
            validationEnabled ? "core validation" : "disabled");

        return {};
    }

    void VulkanInstance::Shutdown()
    {
        m_DebugUtils.Shutdown();

        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }

        m_SurfaceMaintenanceEnabled = false;
    }

}