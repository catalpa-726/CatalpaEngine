#include "catpch.h"
#include "VulkanSwapchain.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"
#include "VulkanBinarySemaphore.h"

namespace Catalpa {

    // 优先采用请求格式；没有匹配项时，在相同 sRGB／UNorm 编码类别中选择受支持的格式
    static PixelFormat SelectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats, PixelFormat preferred, VkSurfaceFormatKHR& selected)
    {
        constexpr std::array mappings{
            std::pair{ PixelFormat::BGRA8_SRGB, VK_FORMAT_B8G8R8A8_SRGB },
            std::pair{ PixelFormat::RGBA8_SRGB, VK_FORMAT_R8G8B8A8_SRGB },
            std::pair{ PixelFormat::BGRA8_UNorm, VK_FORMAT_B8G8R8A8_UNORM },
            std::pair{ PixelFormat::RGBA8_UNorm, VK_FORMAT_R8G8B8A8_UNORM }
        };

        const PixelFormatInfo preferredInfo = GetPixelFormatInfo(preferred);
        if (preferredInfo.bytesPerBlock == 0) return PixelFormat::Unknown;

        PixelFormat chosen = PixelFormat::Unknown;

        for (const auto& [pixelFormat, nativeFormat] : mappings) {
            if (GetPixelFormatInfo(pixelFormat).isSRGB != preferredInfo.isSRGB) continue;

            for (const auto& format : formats) {
                if (format.format != nativeFormat || format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;

                if (chosen == PixelFormat::Unknown || pixelFormat == preferred) {
                    chosen = pixelFormat;
                    selected = format;
                }

                if (pixelFormat == preferred) return chosen;
            }
        }

        return chosen;
    }

    // 优先采用请求模式，不支持时回退到 FIFO
    static std::pair<VkPresentModeKHR, RHIPresentMode> SelectPresentMode(const std::vector<VkPresentModeKHR>& modes, RHIPresentMode preferred)
    {
        constexpr std::array mappings{
            std::pair{ RHIPresentMode::Fifo, VK_PRESENT_MODE_FIFO_KHR },
            std::pair{ RHIPresentMode::Mailbox, VK_PRESENT_MODE_MAILBOX_KHR },
            std::pair{ RHIPresentMode::Immediate, VK_PRESENT_MODE_IMMEDIATE_KHR }
        };

        for (const auto& [mode, nativeMode] : mappings) {
            if (mode == preferred && std::ranges::find(modes, nativeMode) != modes.end()) return { nativeMode, mode };
        }

        return { VK_PRESENT_MODE_FIFO_KHR, RHIPresentMode::Fifo };
    }

    VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device, VkSurfaceKHR surface) : m_Device(device), m_Surface(surface) {}

    VulkanSwapchain::~VulkanSwapchain() { Shutdown(); }

    RHIStatus VulkanSwapchain::Init(const RHISwapchainDesc& desc)
    {
        CAT_CORE_ASSERT(m_Info.state == RHISwapchainState::Uninitialized, "Swapchain already initialized!");

        if (desc.extent.IsEmpty()) {
            m_Info.state = RHISwapchainState::Suspended;
            return {};
        }

        const VkDevice device = m_Device.GetHandle();
        const VkPhysicalDevice physicalDevice = m_Device.GetPhysicalDevice();

        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &capabilities);
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        VkExtent2D extent = capabilities.currentExtent;

        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            extent.width = std::clamp(desc.extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(desc.extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        if (extent.width == 0 || extent.height == 0) {
            m_Info.state = RHISwapchainState::Suspended;
            return {};
        }

        std::vector<VkSurfaceFormatKHR> formats;
        result = EnumerateVulkan(formats, [&](uint32_t* count, VkSurfaceFormatKHR* data) { return vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, count, data); });
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");

        VkSurfaceFormatKHR surfaceFormat{};
        const PixelFormat pixelFormat = SelectSurfaceFormat(formats, desc.preferredFormat, surfaceFormat);

        if (pixelFormat == PixelFormat::Unknown) {
            CAT_CORE_ERROR("No compatible SDR surface format found.");
            return { .code = RHIErrorCode::Unsupported, .operation = "VulkanSwapchain::SelectSurfaceFormat" };
        }

        std::vector<VkPresentModeKHR> presentModes;
        result = EnumerateVulkan(presentModes, [&](uint32_t* count, VkPresentModeKHR* data) { return vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, count, data); });
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");

        const auto [nativePresentMode, presentMode] = SelectPresentMode(presentModes, desc.preferredPresentMode);

        uint32_t imageCount = std::max(desc.preferredImageCount, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0) imageCount = std::min(imageCount, capabilities.maxImageCount);

        constexpr std::array alphaModes{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };

        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        for (const auto mode : alphaModes) {
            if (capabilities.supportedCompositeAlpha & mode) {
                compositeAlpha = mode;
                break;
            }
        }

        const std::array families{ m_Device.GetGraphicsQueueFamily(), m_Device.GetPresentQueueFamily() };
        const bool concurrent = families[0] != families[1];

        VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = m_Surface,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = concurrent ? 2u : 0u,
            .pQueueFamilyIndices = concurrent ? families.data() : nullptr,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = compositeAlpha,
            .presentMode = nativePresentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkCreateSwapchainKHR");
        m_Swapchain = swapchain;

        result = EnumerateVulkan(m_Images, [&](uint32_t* count, VkImage* data) { return vkGetSwapchainImagesKHR(device, m_Swapchain, count, data); });
        if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkGetSwapchainImagesKHR");

        m_ImageViews.reserve(m_Images.size());
        m_PresentSemaphores.reserve(m_Images.size());

        for (const VkImage image : m_Images) {
            VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surfaceFormat.format,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
            };

            VkImageView view = VK_NULL_HANDLE;
            result = vkCreateImageView(device, &viewInfo, nullptr, &view);
            if (result != VK_SUCCESS) return MakeVulkanStatus(result, "vkCreateImageView");
            m_ImageViews.push_back(view);

            auto semaphore = std::make_unique<VulkanBinarySemaphore>(m_Device);
            if (const RHIStatus status = semaphore->Init(); !status.IsSuccess()) return status;
            m_PresentSemaphores.push_back(std::move(semaphore));
        }

        m_Info = {
            .state = RHISwapchainState::Ready,
            .extent = { extent.width, extent.height },
            .format = pixelFormat,
            .presentMode = presentMode,
            .imageCount = static_cast<unsigned int>(m_Images.size())
        };

        CAT_CORE_INFO("Vulkan swapchain created: {0}x{1}, {2} images.", extent.width, extent.height, m_Info.imageCount);
        return {};
    }

    RHIStatus VulkanSwapchain::Recreate(const RHISwapchainDesc& desc)
    {
        Shutdown();

        const RHIStatus status = Init(desc);
        if (status.IsSuccess()) return status;

        Shutdown();
        if (status.nativeCode == VK_ERROR_SURFACE_LOST_KHR) m_Info.state = RHISwapchainState::SurfaceLost;
        return status;
    }

    void VulkanSwapchain::Shutdown()
    {
        m_PresentSemaphores.clear();

        for (const VkImageView view : m_ImageViews) vkDestroyImageView(m_Device.GetHandle(), view, nullptr);
        m_ImageViews.clear();
        m_Images.clear();

        if (m_Swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(m_Device.GetHandle(), m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
        m_Info = {};
    }

}