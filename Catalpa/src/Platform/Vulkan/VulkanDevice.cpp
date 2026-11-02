#include "catpch.h"
#include "Log/Log.h"
#include "VulkanDevice.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanLoader.h"
#include "VulkanBackend.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "VulkanFrameContext.h"
#include "VulkanQueue.h"
#include "VulkanResourceRegistry.h"

namespace Catalpa {

    // 当前由主线程签发；计数器不随设备关闭而复位。
    static DeviceId AllocateDeviceId()
    {
        static uint64_t next = 0;

        if (next == std::numeric_limits<uint64_t>::max()) {
            CAT_CORE_ASSERT(false, "Device identity space exhausted!");
            std::abort();
        }

        return { .value = ++next };
    }

    VulkanDevice::VulkanDevice() = default;

    VulkanDevice::~VulkanDevice() { Shutdown(); }

    bool VulkanDevice::Init(std::unique_ptr<VulkanBackend> backend)
    {
        CAT_CORE_ASSERT(m_Device == VK_NULL_HANDLE, "Vulkan device already exists!");

        const VkInstance instance = backend->GetInstance().GetHandle();
        const VulkanPhysicalDevice& physicalDevice = backend->GetPhysicalDevice();

        const VkPhysicalDevice physicalHandle = physicalDevice.GetHandle();
        const std::array families{ physicalDevice.GetGraphicsQueueFamily(), physicalDevice.GetPresentQueueFamily() };
        const uint32_t familyCount = families[0] == families[1] ? 1u : 2u;
        const float priority = 1.0f;

        std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};

        for (uint32_t i = 0; i < familyCount; ++i) {
            queueInfos[i] = VkDeviceQueueCreateInfo{
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = families[i],
                .queueCount = 1,
                .pQueuePriorities = &priority
            };
        }

        VkPhysicalDeviceVulkan13Features enabledFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE
        };

        constexpr std::array extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &enabledFeatures,
            .queueCreateInfoCount = familyCount,
            .pQueueCreateInfos = queueInfos.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };

        VkDevice device = VK_NULL_HANDLE;

        if (const VkResult result = vkCreateDevice(physicalHandle, &createInfo, nullptr, &device); result != VK_SUCCESS) {
            CAT_CORE_ERROR("Failed to create Vulkan device. VkResult: {0}", static_cast<int>(result));
            return false;
        }

        if (!VulkanLoader::Load(instance, physicalHandle, device)) {
            vkDestroyDevice(device, nullptr);
            return false;
        }

        VkQueue graphicsQueue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, families[0], 0, &graphicsQueue);
        vkGetDeviceQueue(device, families[1], 0, &presentQueue);

        m_Device = device;
        m_PhysicalDevice = physicalHandle;
        m_PresentQueue = presentQueue;
        m_GraphicsQueueFamily = families[0];
        m_PresentQueueFamily = families[1];

        m_Backend = std::move(backend);
        m_GraphicsQueue = std::make_unique<VulkanQueue>(graphicsQueue, m_GraphicsQueueFamily);
        m_ResourceRegistry = std::make_unique<VulkanResourceRegistry>(AllocateDeviceId());

        for (auto& frame : m_Frames) {
            frame = std::make_unique<VulkanFrameContext>(*this);

            if (const RHIStatus status = frame->Init(m_GraphicsQueueFamily); !status.IsSuccess()) {
                Shutdown();
                return false;
            }
        }

        CAT_CORE_INFO("Vulkan device created.");
        return true;
    }

    RHIStatus VulkanDevice::WaitIdle() const { 
        return MakeVulkanStatus(vkDeviceWaitIdle(m_Device), "vkDeviceWaitIdle");
    }

    std::unique_ptr<RHISwapchain> VulkanDevice::CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status)
    {
        auto swapchain = std::make_unique<VulkanSwapchain>(*this, m_Backend->GetSurface().GetHandle());
        status = swapchain->Init(desc);
        if (!status.IsSuccess()) return nullptr;
        return swapchain;
    }

    void VulkanDevice::Shutdown()
    {
        for (auto& frame : m_Frames) frame.reset();
        m_GraphicsQueue.reset();

        if (m_Device != VK_NULL_HANDLE) {
            m_ResourceRegistry.reset();
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
            m_PhysicalDevice = VK_NULL_HANDLE;
            m_PresentQueue = VK_NULL_HANDLE;
            m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
            m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        }

        m_Backend.reset();
    }

    DeviceId VulkanDevice::GetDeviceId() const
    {
        return m_ResourceRegistry ? m_ResourceRegistry->GetDeviceId() : DeviceId{};
    }

}