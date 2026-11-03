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
#include "VulkanConversions.h"

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

        const bool maintenanceEnabled = physicalDevice.SupportsSwapchainMaintenance();

        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
            .swapchainMaintenance1 = VK_TRUE
        };

        VkPhysicalDeviceVulkan13Features enabledFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = maintenanceEnabled ? &maintenance : nullptr,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE
        };

        std::vector<const char*> extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        if (maintenanceEnabled) extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

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

        const auto& limits = physicalDevice.GetProperties().limits;

        m_Capabilities = {
            .maxTextureDimension2D = limits.maxImageDimension2D,
            .maxTextureArrayLayers = limits.maxImageArrayLayers,
            .maxColorAttachments = limits.maxColorAttachments,
            .maxRenderTargetExtent = { limits.maxFramebufferWidth, limits.maxFramebufferHeight },
            .maxPushConstantBytes = limits.maxPushConstantsSize,
            .maxUniformBufferRange = limits.maxUniformBufferRange,
            .maxStorageBufferRange = limits.maxStorageBufferRange,
            .minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment,
            .minStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment,
            .presentFence = maintenanceEnabled
        };

        m_Backend = std::move(backend);
        m_ResourceRegistry = std::make_unique<VulkanResourceRegistry>(*this, AllocateDeviceId());
        m_GraphicsQueue = std::make_unique<VulkanQueue>(*this, graphicsQueue, m_GraphicsQueueFamily, QueueId{ 1 });

        for (uint32_t slot = 0; slot < FramesInFlight; ++slot) {
            auto& frame = m_Frames[slot];
            frame = std::make_unique<VulkanFrameContext>(*this, slot);

            if (const RHIStatus status = frame->Init(m_GraphicsQueueFamily); !status.IsSuccess()) {
                Shutdown();
                return false;
            }
        }

        CAT_CORE_INFO("Vulkan presentation synchronization: {0}.", maintenanceEnabled ? "EXT maintenance present fences" : "WaitIdle compatibility fallback");
        CAT_CORE_INFO("Vulkan device created.");
        return true;
    }

    RHIStatus VulkanDevice::WaitIdle() const { return MakeVulkanStatus(vkDeviceWaitIdle(m_Device), "vkDeviceWaitIdle"); }

    std::unique_ptr<RHISwapchain> VulkanDevice::CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status)
    {
        auto swapchain = std::make_unique<VulkanSwapchain>(*this, m_Backend->GetSurface().GetHandle());
        status = ObserveStatus(swapchain->Init(desc));
        if (!status.IsSuccess()) return nullptr;
        return swapchain;
    }

    RHIFormatSupport VulkanDevice::GetFormatSupport(PixelFormat format) const
    {
        const VkFormat nativeFormat = ToVulkanFormat(format);
        if (nativeFormat == VK_FORMAT_UNDEFINED) return {};

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, nativeFormat, &properties);

        const VkFormatFeatureFlags features = properties.optimalTilingFeatures;

        return {
            .sampledImage = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0,
            .linearFiltering = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0,
            .storageImage = (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0,
            .colorAttachment = (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0,
            .colorAttachmentBlend = (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0,
            .transferSource = (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0,
            .transferDestination = (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0
        };
    }

    DeviceId VulkanDevice::GetDeviceId() const { return m_ResourceRegistry ? m_ResourceRegistry->GetDeviceId() : DeviceId{}; }

    RHIStatus VulkanDevice::WaitIdleAndDiscardFrameCommands()
    {
        if (!m_DeviceFailure.IsSuccess() && std::ranges::none_of(m_Frames, [](const auto& frame) { return static_cast<bool>(frame); }))
            return m_DeviceFailure;

        RHIStatus status = WaitIdle();

        // 已经失设备，后续 WaitIdle 成功也不恢复正常运行。
        if (status.IsSuccess() && !m_DeviceFailure.IsSuccess()) status = m_DeviceFailure;

        if (status.IsSuccess()) {
            for (auto& frame : m_Frames) {
                if (!frame) continue;

                bool ready = false;
                status = frame->PrepareReuse(0, ready);
                if (!status.IsSuccess()) break;

                if (!ready) {
                    status = { .code = RHIErrorCode::InvalidState, .operation = "VulkanDevice::WaitIdleAndDiscardFrameCommands" };
                    break;
                }
            }
        }

        if (status.code == RHIErrorCode::DeviceLost) {
            ObserveStatus(status);
            if (m_GraphicsQueue) m_GraphicsQueue->DiscardPendingSubmissions();
            for (auto& frame : m_Frames) frame.reset();
        }

        return status;
    }

    void VulkanDevice::Shutdown()
    {
        if (m_GraphicsQueue) m_GraphicsQueue->DiscardPendingSubmissions();
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
        m_Capabilities = {};
        m_DeviceFailure = {};
    }

    RHIStatus VulkanDevice::ObserveStatus(RHIStatus status)
    {
        if (status.code == RHIErrorCode::DeviceLost) m_DeviceFailure = status;
        return status;
    }

    VulkanFrameContext* VulkanDevice::ResolveFrame(RHIFrameToken token)
    {
        if (!token.HasValue() || token.device != GetDeviceId() || token.slot >= FramesInFlight) return nullptr;

        auto* frame = m_Frames[token.slot].get();
        return frame && frame->GetToken() == token ? frame : nullptr;
    }

    RHIStatus VulkanDevice::PrepareFrame(uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& resources, bool& ready)
    {
        resources = {};
        ready = false;
        if (!m_DeviceFailure.IsSuccess()) return m_DeviceFailure;

        if (slot >= FramesInFlight)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanDevice::PrepareFrame" };

        auto& frame = *m_Frames[slot];

        if (frame.HasActiveAcquisition())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanDevice::PrepareFrame" };

        const RHIStatus status = ObserveStatus(frame.PrepareReuse(timeoutNanoseconds, ready));
        if (status.IsSuccess() && ready) resources = { .token = frame.GetToken(), .commands = &frame.GetCommandList() };
        return status;
    }

    RHIStatus VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline)
    {
        pipeline = {};
        if (!m_DeviceFailure.IsSuccess()) return m_DeviceFailure;
        return ObserveStatus(m_ResourceRegistry->CreateGraphicsPipeline(desc, pipeline));
    }

    void VulkanDevice::ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) { m_ResourceRegistry->ReleaseGraphicsPipeline(pipeline); }

}