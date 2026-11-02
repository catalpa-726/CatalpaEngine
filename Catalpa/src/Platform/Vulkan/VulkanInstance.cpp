#include "catpch.h"
#include "VulkanInstance.h"
#include "VulkanLoader.h"
#include "Log/Log.h"

namespace Catalpa {

    VulkanInstance::~VulkanInstance()
    {
        Shutdown();
    }

    bool VulkanInstance::Init(const std::vector<const char*>& extensions, uint32_t apiVersion)
    {
        CAT_CORE_ASSERT(m_Instance == VK_NULL_HANDLE, "Vulkan instance already exists!");

        if (!VulkanLoader::Load()) {
            return false;
        }

        VkApplicationInfo appInfo{
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = "CatalpaEngine",
                .pEngineName = "CatalpaEngine",
                .apiVersion = apiVersion
        };
       

        VkInstanceCreateInfo createInfo{
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &appInfo,
                .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                .ppEnabledExtensionNames = extensions.data()
        };
        

        VkInstance instance = VK_NULL_HANDLE;
        const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

        if (result != VK_SUCCESS) {
            CAT_CORE_ERROR("Failed to create Vulkan instance. VkResult: {0}", static_cast<int>(result));
            return false;
        }

        if (!VulkanLoader::Load(instance)) {
            vkDestroyInstance(instance, nullptr);
            return false;
        }

        m_Instance = instance;
        CAT_CORE_INFO("Vulkan instance created.");
        return true;
    }

    void VulkanInstance::Shutdown()
    {
        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }
    }

}