#include "catpch.h"
#include "VulkanPipelineCache.h"
#include "Log/Log.h"
#include "VulkanDevice.h"
#include "VulkanQueryPool.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanLoader.h"
#include "VulkanBackend.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "VulkanFrameContext.h"
#include "VulkanAuxiliaryContext.h"
#include "VulkanQueue.h"
#include "VulkanResourceRegistry.h"
#include "VulkanConversions.h"
#include "VulkanMemoryAllocator.h"
#include "VulkanDebugUtils.h"
#include "VulkanResourceStateTracker.h"

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
        VkPhysicalDeviceVulkan13Features supported13{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features supported12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supported13};
        VkPhysicalDeviceVulkan11Features supported11{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &supported12};
        VkPhysicalDeviceFeatures2 supported{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                            .pNext = &supported11};
        vkGetPhysicalDeviceFeatures2(physicalHandle, &supported);
        // SV_InstanceID等SPIR-V产物会声明DrawParameters；支持查询不等于已启用。
        if (!supported11.shaderDrawParameters || !supported13.synchronization2 ||
            !supported13.dynamicRendering) {
            CAT_CORE_ERROR("Vulkan device lacks required "
                           "shaderDrawParameters/synchronization2/dynamicRendering features.");
            return false;
        }
        const std::array families{physicalDevice.GetGraphicsQueueFamily(),
                                  physicalDevice.GetPresentQueueFamily()};
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalHandle, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueProperties(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalHandle, &queueFamilyCount, queueProperties.data());
        uint32_t computeFamily = families[0], transferFamily = families[0], computeIndex = 0,
                 transferIndex = 0;
        if (supported12.timelineSemaphore) {
            for (uint32_t i = 0; i < queueFamilyCount; ++i)
                if (queueProperties[i].queueCount && (queueProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                    !(queueProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    computeFamily = i;
                    break;
                }
            if (computeFamily == families[0] && queueProperties[computeFamily].queueCount > 1)
                computeIndex = 1;
            for (uint32_t i = 0; i < queueFamilyCount; ++i)
                if (queueProperties[i].queueCount &&
                    (queueProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    !(queueProperties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
                    transferFamily = i;
                    break;
                }
            if (transferFamily == families[0]) {
                if (queueProperties[families[0]].queueCount > 2)
                    transferIndex = 2;
                else if (computeFamily != families[0]) {
                    transferFamily = computeFamily;
                    transferIndex = queueProperties[computeFamily].queueCount > 1 ? 1 : computeIndex;
                } else
                    transferIndex = computeIndex;
            }
        }
        std::map<uint32_t, uint32_t> familyRequests{{families[0], 1}, {families[1], 1}};
        familyRequests[computeFamily] = std::max(familyRequests[computeFamily], computeIndex + 1);
        familyRequests[transferFamily] = std::max(familyRequests[transferFamily], transferIndex + 1);
        const std::array<float, 3> priorities{1, 1, 1};
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        queueInfos.reserve(familyRequests.size());
        for (const auto [family, count] : familyRequests)
            queueInfos.push_back({.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = family,
                                  .queueCount = count,
                                  .pQueuePriorities = priorities.data()});
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
        VkPhysicalDeviceVulkan12Features enabled12{.sType =
                                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                   .pNext = &enabledFeatures,
                                                   .timelineSemaphore = supported12.timelineSemaphore};
        VkPhysicalDeviceVulkan11Features enabled11{.sType =
                                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                                   .pNext = &enabled12,
                                                   .shaderDrawParameters = VK_TRUE};
        VkPhysicalDeviceFeatures basic{};
        basic.textureCompressionBC = supported.features.textureCompressionBC;

        std::vector<const char*> extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        if (maintenanceEnabled) extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        uint32_t extensionCount = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalHandle, nullptr, &extensionCount, nullptr) !=
            VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        if (vkEnumerateDeviceExtensionProperties(physicalHandle, nullptr, &extensionCount,
                                                 availableExtensions.data()) != VK_SUCCESS)
            return false;
        bool memoryBudgetEnabled = false;
#if defined(VK_EXT_memory_budget)
        memoryBudgetEnabled =
            std::any_of(availableExtensions.begin(), availableExtensions.end(), [](const auto& extension) {
                return std::strcmp(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
            });
        if (memoryBudgetEnabled)
            extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif

        VkDeviceCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .pNext = &enabled11,
                                      .queueCreateInfoCount = uint32_t(queueInfos.size()),
                                      .pQueueCreateInfos = queueInfos.data(),
                                      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                                      .ppEnabledExtensionNames = extensions.data(),
                                      .pEnabledFeatures = &basic};

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

        const bool computeSupported = computeFamily < queueProperties.size() &&
                                      (queueProperties[computeFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        const auto& limits = physicalDevice.GetProperties().limits;

        m_Capabilities = {.maxTextureDimension2D = limits.maxImageDimension2D,
                          .maxTextureArrayLayers = limits.maxImageArrayLayers,
                          .maxColorAttachments = limits.maxColorAttachments,
                          .maxRenderTargetExtent = {limits.maxFramebufferWidth, limits.maxFramebufferHeight},
                          .maxVertexInputAttributes = limits.maxVertexInputAttributes,
                          .maxVertexInputAttributeOffset = limits.maxVertexInputAttributeOffset,
                          .maxVertexInputBindingStride = limits.maxVertexInputBindingStride,
                          .maxPushConstantBytes = limits.maxPushConstantsSize,
                          .maxUniformBufferRange = limits.maxUniformBufferRange,
                          .maxStorageBufferRange = limits.maxStorageBufferRange,
                          .minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment,
                          .minStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment,
                          .compute = computeSupported,
                          .drawIndirect = true,
                          .maxComputeWorkGroupCount = {limits.maxComputeWorkGroupCount[0],
                                                       limits.maxComputeWorkGroupCount[1],
                                                       limits.maxComputeWorkGroupCount[2]},
                          .presentFence = maintenanceEnabled,
                          .maxSamplerLodBias = limits.maxSamplerLodBias};

        m_Backend = std::move(backend);
        const auto& debug = GetDebugUtils();
        debug.NameObject(device, VK_OBJECT_TYPE_DEVICE, VulkanHandleToUint64(device), "Catalpa Device");
        debug.NameObject(device, VK_OBJECT_TYPE_QUEUE, VulkanHandleToUint64(graphicsQueue),
            graphicsQueue == presentQueue ? "Graphics/Present Queue" : "Graphics Queue");

        if (graphicsQueue != presentQueue)
            debug.NameObject(device, VK_OBJECT_TYPE_QUEUE, VulkanHandleToUint64(presentQueue), "Present Queue");

        m_MemoryAllocator = std::make_unique<VulkanMemoryAllocator>();

        if (const RHIStatus status =
                m_MemoryAllocator->Init(instance, m_PhysicalDevice, m_Device, memoryBudgetEnabled);
            !status.IsSuccess()) {
            Shutdown();
            return false;
        }

        m_PipelineCache = std::make_unique<VulkanPipelineCache>(m_Device, physicalDevice.GetProperties());
        m_ResourceRegistry = std::make_unique<VulkanResourceRegistry>(*this, AllocateDeviceId());
        m_TimelineEnabled = supported12.timelineSemaphore != 0;
        m_GraphicsQueue =
            std::make_unique<VulkanQueue>(*this, graphicsQueue, m_GraphicsQueueFamily, QueueId{1});
        if (const auto status = m_GraphicsQueue->InitSynchronization(); !status.IsSuccess()) {
            Shutdown();
            return false;
        }
        const auto info = [&](QueueId id, uint32_t family) {
            const auto flags = queueProperties[family].queueFlags;
            return RHIQueueInfo{
                id, family, bool(flags & VK_QUEUE_GRAPHICS_BIT), bool(flags & VK_QUEUE_COMPUTE_BIT),
                bool(flags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT))};
        };
        m_QueueInfos.push_back(info({1}, families[0]));
        if (computeFamily != families[0] || computeIndex) {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device, computeFamily, computeIndex, &queue);
            m_ComputeQueue = std::make_unique<VulkanQueue>(*this, queue, computeFamily, QueueId{2});
            if (const auto status = m_ComputeQueue->InitSynchronization(); !status.IsSuccess()) {
                Shutdown();
                return false;
            }
            m_QueueInfos.push_back(info({2}, computeFamily));
        }
        const bool transferIsGraphics = transferFamily == families[0] && transferIndex == 0;
        const bool transferIsCompute =
            m_ComputeQueue && transferFamily == computeFamily && transferIndex == computeIndex;
        if (!transferIsGraphics && !transferIsCompute) {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device, transferFamily, transferIndex, &queue);
            m_TransferQueue = std::make_unique<VulkanQueue>(*this, queue, transferFamily, QueueId{3});
            if (const auto status = m_TransferQueue->InitSynchronization(); !status.IsSuccess()) {
                Shutdown();
                return false;
            }
            m_QueueInfos.push_back(info({3}, transferFamily));
        }

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
    RHIStatus VulkanDevice::GetMemoryBudgets(std::vector<RHIMemoryHeapBudget>& output) const {
        if (!m_MemoryAllocator)
            return {RHIErrorCode::InvalidState, 0, "VulkanDevice/MemoryBudgets"};
        return m_MemoryAllocator->GetBudgets(output);
    }

    std::unique_ptr<RHISwapchain> VulkanDevice::CreateSwapchain(const RHISwapchainDesc& desc, RHIStatus& status)
    {
        auto swapchain = std::make_unique<VulkanSwapchain>(*this, m_Backend->GetSurface().GetHandle());
        status = ObserveStatus(swapchain->Init(desc));
        if (!status.IsSuccess()) return nullptr;
        return swapchain;
    }

    std::unique_ptr<RHISwapchain> VulkanDevice::CreateWindowSwapchain(void* window, const RHISwapchainDesc& desc,
                                                                     RHIStatus& status)
    {
        auto surface = std::make_unique<VulkanSurface>();
        if (!surface->InitNative(m_Backend->GetInstance().GetHandle(), window)) {
            status = {RHIErrorCode::BackendFailure, 0, "Create platform window surface"};
            return nullptr;
        }
        VkBool32 supported = VK_FALSE;
        status = ObserveStatus(MakeVulkanStatus(vkGetPhysicalDeviceSurfaceSupportKHR(
            m_PhysicalDevice, GetPresentQueueFamily(), surface->GetHandle(), &supported), "Platform surface support"));
        if (!status.IsSuccess()) return nullptr;
        if (!supported) {
            status = {RHIErrorCode::Unsupported, 0, "Present queue cannot present to this window"};
            return nullptr;
        }
        auto chain = std::make_unique<VulkanSwapchain>(*this, std::move(surface));
        status = ObserveStatus(chain->Init(desc));
        if (!status.IsSuccess()) return nullptr;
        return chain;
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
            .transferDestination = (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0,
            .depthAttachment = (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0
        };
    }

    const VulkanDebugUtils& VulkanDevice::GetDebugUtils() const { return m_Backend->GetInstance().GetDebugUtils(); }

    VkPipelineCache VulkanDevice::GetPipelineCache() const { return m_PipelineCache ? m_PipelineCache->GetHandle() : VK_NULL_HANDLE; }

    DeviceId VulkanDevice::GetDeviceId() const { return m_ResourceRegistry ? m_ResourceRegistry->GetDeviceId() : DeviceId{}; }

    RHIStatus VulkanDevice::WaitIdleAndDiscardFrameCommands()
    {
        if (!m_DeviceFailure.IsSuccess() && std::ranges::none_of(m_Frames, [](const auto& frame) { return static_cast<bool>(frame); }))
            return m_DeviceFailure;

        RHIStatus status = WaitIdle();

        // 已经失设备，后续 WaitIdle 成功也不恢复正常运行。
        if (status.IsSuccess() && !m_DeviceFailure.IsSuccess()) status = m_DeviceFailure;

        if (status.IsSuccess()) {
            for (auto& contexts : m_Auxiliary)
                for (auto& context : contexts)
                    if (context) {
                        bool ready = false;
                        status = context->PrepareReuse(0, ready);
                        if (!status.IsSuccess() || !ready) {
                            if (status.IsSuccess())
                                status = {RHIErrorCode::InvalidState, 0, "VulkanDevice/AuxiliaryNotIdle"};
                            return status;
                        }
                    }
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
            if (m_ComputeQueue)
                m_ComputeQueue->DiscardPendingSubmissions();
            if (m_TransferQueue)
                m_TransferQueue->DiscardPendingSubmissions();
            for (auto& contexts : m_Auxiliary)
                contexts.clear();
            for (auto& frame : m_Frames) frame.reset();
        }

        return status;
    }

    void VulkanDevice::Shutdown()
    {
        if (m_GraphicsQueue) m_GraphicsQueue->DiscardPendingSubmissions();
        if (m_ComputeQueue)
            m_ComputeQueue->DiscardPendingSubmissions();
        if (m_TransferQueue)
            m_TransferQueue->DiscardPendingSubmissions();
        for (auto& contexts : m_Auxiliary)
            contexts.clear();
        for (auto& frame : m_Frames) frame.reset();
        m_TransferQueue.reset();
        m_ComputeQueue.reset();
        m_QueueInfos.clear();
        m_TimelineEnabled = false;
        m_GraphicsQueue.reset();

        if (m_Device != VK_NULL_HANDLE) {
            m_ResourceRegistry.reset();
            m_PipelineCache.reset();
            m_MemoryAllocator.reset();
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
        std::lock_guard lock(m_StatusMutex);
        if (status.code == RHIErrorCode::DeviceLost) m_DeviceFailure = status;
        return status;
    }

    VulkanFrameContext* VulkanDevice::ResolveFrame(RHIFrameToken token)
    {
        if (!token.HasValue() || token.device != GetDeviceId() || token.slot >= FramesInFlight) return nullptr;

        auto* frame = m_Frames[token.slot].get();
        return frame && frame->GetToken() == token ? frame : nullptr;
    }
    RHIQueue* VulkanDevice::GetQueue(QueueId id) {
        if (m_GraphicsQueue && m_GraphicsQueue->GetId() == id)
            return m_GraphicsQueue.get();
        if (m_ComputeQueue && m_ComputeQueue->GetId() == id)
            return m_ComputeQueue.get();
        if (m_TransferQueue && m_TransferQueue->GetId() == id)
            return m_TransferQueue.get();
        return nullptr;
    }
    RHIQueue& VulkanDevice::GetQueue(RHIQueueClass kind) {
        if (kind == RHIQueueClass::Transfer && m_TransferQueue)
            return *m_TransferQueue;
        if (kind != RHIQueueClass::Graphics && m_ComputeQueue)
            return *m_ComputeQueue;
        return *m_GraphicsQueue;
    }
    RHIQueueInfo VulkanDevice::GetQueueInfo(QueueId id) const {
        for (const auto& info : m_QueueInfos)
            if (info.id == id)
                return info;
        return {};
    }
    VulkanAuxiliaryContext* VulkanDevice::ResolveAuxiliary(RHIFrameToken token, RHICommandList* commands,
                                                           QueueId queue) {
        if (!ResolveFrame(token))
            return nullptr;
        for (auto& context : m_Auxiliary[token.slot])
            if (context && context->generation == token.generation && context->Queue() == queue &&
                &context->Commands() == commands)
                return context.get();
        return nullptr;
    }
    RHIStatus VulkanDevice::PrepareAuxiliaryCommands(RHIFrameToken token, QueueId queue, uint32_t ordinal,
                                                     RHIFrameResources& output) try {
        output = {};
        auto* frame = ResolveFrame(token);
        // 最多128个载荷批次，每批跨三个实际queue修复所有权；draw数量不消耗额外上下文。
        if (!frame || !frame->HasActiveAcquisition() || !GetQueue(queue) || !m_TimelineEnabled ||
            ordinal >= 1280)
            return {RHIErrorCode::InvalidDescription, 0, "VulkanDevice/AuxiliaryFrameQueueBudget"};
        auto& contexts = m_Auxiliary[token.slot];
        if (ordinal > contexts.size())
            return {RHIErrorCode::InvalidDescription, 0, "VulkanDevice/AuxiliaryOrdinal"};
        if (ordinal == contexts.size())
            contexts.push_back(nullptr);
        auto& context = contexts[ordinal];
        if (context && context->generation)
            return {RHIErrorCode::InvalidState, 0, "VulkanDevice/AuxiliaryAlreadyAssigned"};
        if (!context || context->Queue() != queue) {
            auto candidate = std::make_unique<VulkanAuxiliaryContext>(*this, queue);
            if (const auto status = candidate->Init(GetQueueInfo(queue).family); !status.IsSuccess())
                return status;
            context = std::move(candidate);
        }
        context->generation = token.generation;
        output = {token, &context->Commands()};
        return {};
    } catch (const std::bad_alloc&) {
        return {RHIErrorCode::OutOfMemory, 0, "VulkanDevice/AuxiliaryAllocation"};
    }

    RHIStatus VulkanDevice::PrepareFrame(uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& resources, bool& ready)
    {
        resources = {};
        ready = false;
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;

        if (slot >= FramesInFlight)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanDevice::PrepareFrame" };

        auto& frame = *m_Frames[slot];

        if (frame.HasActiveAcquisition())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanDevice::PrepareFrame" };

        // presentation完成点不能代替其它queue的Fence观察；部分submit失败也须逐context收束。
        for (auto& context : m_Auxiliary[slot])
            if (context) {
                bool available = false;
                const auto status = ObserveStatus(context->PrepareReuse(timeoutNanoseconds, available));
                if (!status.IsSuccess() || !available)
                    return status;
            }

        const RHIStatus status = ObserveStatus(frame.PrepareReuse(timeoutNanoseconds, ready));
        if (status.IsSuccess() && ready) resources = { .token = frame.GetToken(), .commands = &frame.GetCommandList() };
        return status;
    }

    RHIStatus VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc,
                                                  ComputePipelineHandle& pipeline) {
        try {
            return ObserveStatus(m_ResourceRegistry->CreateComputePipeline(desc, pipeline));
        } catch (const std::bad_alloc&) {
            return {RHIErrorCode::OutOfMemory, 0, "CreateComputePipeline"};
        }
    }

    RHIStatus VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline)
    {
        pipeline = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->CreateGraphicsPipeline(desc, pipeline));
    }

    void VulkanDevice::ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) { m_ResourceRegistry->ReleaseGraphicsPipeline(pipeline); }

    RHIStatus VulkanDevice::CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer)
    {
        buffer = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->CreateBuffer(desc, buffer));
    }

    RHIStatus VulkanDevice::WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data)
    {
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->WriteBuffer(buffer, offset, data));
    }

    // 读回只消费已经完成且包含最近使用的提交，不把任意旧 Fence 当作本次复制的证明。
    RHIStatus VulkanDevice::ReadBuffer(BufferHandle buffer, uint64_t offset, std::span<std::byte> data, RHICompletionPoint completion)
    {
        constexpr const char* operation = "VulkanDevice::ReadBuffer";
        RHIBufferStateSnapshot snapshot;
        if (const auto status = GetBufferState(buffer, snapshot); !status.IsSuccess()) return status;
        const auto& last = snapshot.state.lastUse;
        if (snapshot.desc.memory != RHIMemoryDomain::Readback || data.empty()
            || !IsValidBufferRange(snapshot.desc.size, offset, data.size())
            || !RangeContains(snapshot.state.initialized, offset, data.size())
            || snapshot.state.access.access != RHIResourceAccess::HostRead
            || !last.HasValue() || completion.device != last.device || completion.queue != last.queue
            || completion.sequence < last.sequence)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        bool complete = false;
        if (const auto status = QueryCompletion(completion, complete); !status.IsSuccess())
            return status;
        if (!complete) return { .code = RHIErrorCode::InvalidState, .operation = operation };
        const auto* record = m_ResourceRegistry->ResolveBuffer(buffer);
        if (!record || !record->mapped) return { .code = RHIErrorCode::InvalidState, .operation = operation };
        const auto result = vmaInvalidateAllocation(GetMemoryAllocator().GetHandle(), record->allocation, offset, data.size());
        if (result != VK_SUCCESS) return ObserveStatus(MakeVulkanStatus(result, "vmaInvalidateAllocation"));
        std::memcpy(data.data(), record->mapped + static_cast<size_t>(offset), data.size());
        return {};
    }

    RHIStatus VulkanDevice::CreateQueryPool(const RHIQueryDesc& desc, std::shared_ptr<RHIQueryPool>& pool)
    {
        pool.reset();
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        try {
            auto candidate = std::make_shared<VulkanQueryPool>(*this);
            if (const auto status = candidate->Init(desc); !status.IsSuccess()) return ObserveStatus(status);
            pool = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) {
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanDevice::CreateQueryPool" };
        }
    }

    void VulkanDevice::ReleaseBuffer(BufferHandle buffer) { m_ResourceRegistry->ReleaseBuffer(buffer); }

    RHIStatus VulkanDevice::GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const
    {
        snapshot = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;

        if (!m_ResourceRegistry)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanDevice::GetBufferState" };

        return VulkanResourceStateTracker::Query(*m_ResourceRegistry, buffer, snapshot);
    }

    RHIStatus VulkanDevice::GetTextureState(TextureViewHandle view, RHITextureStateSnapshot& snapshot) const
    {
        snapshot = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;

        if (!m_ResourceRegistry)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanDevice::GetTextureState" };

        return VulkanResourceStateTracker::Query(*m_ResourceRegistry, view, snapshot);
    }

    RHIStatus VulkanDevice::CreateTexture(const RHITextureDesc& desc, TextureHandle& texture)
    {
        texture = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->CreateTexture(desc, texture));
    }

    RHIStatus VulkanDevice::CreateTextureView(TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view)
    {
        view = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->CreateTextureView(texture, desc, view));
    }

    void VulkanDevice::ReleaseTexture(TextureHandle texture) { m_ResourceRegistry->ReleaseTexture(texture); }

    void VulkanDevice::ReleaseTextureView(TextureViewHandle view) { m_ResourceRegistry->ReleaseTextureView(view); }

    RHIStatus VulkanDevice::CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler)
    {
        sampler = {};
        if (const auto failure = GetFailure(); !failure.IsSuccess())
            return failure;
        return ObserveStatus(m_ResourceRegistry->CreateSampler(desc, sampler));
    }

    void VulkanDevice::ReleaseSampler(SamplerHandle sampler) { m_ResourceRegistry->ReleaseSampler(sampler); }

}
