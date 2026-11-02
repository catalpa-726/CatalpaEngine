#pragma once

#include "RHI/RHISwapchain.h"
#include "RHI/RHIError.h"
#include "VulkanHeaders.h"

// 持有 Vulkan 交换链及其图像视图，图像本身是从交换链借用的
namespace Catalpa {

    class VulkanDevice;
    class VulkanBinarySemaphore;

    class VulkanSwapchain : public RHISwapchain {
    public:
        VulkanSwapchain(const VulkanDevice& device, VkSurfaceKHR surface);
        ~VulkanSwapchain() override;

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        [[nodiscard]] RHIStatus Init(const RHISwapchainDesc& desc);
        void Shutdown() override;

        const RHISwapchainInfo& GetInfo() const override { return m_Info; }
        VkSwapchainKHR GetHandle() const { return m_Swapchain; }

        // 供后端按有效图像索引取得图像及视图
        VkImage GetImage(uint32_t index) const { return m_Images[index]; }
        VkImageView GetImageView(uint32_t index) const { return m_ImageViews[index]; }

        [[nodiscard]] RHIStatus Recreate(const RHISwapchainDesc& desc) override;

        // 取得某张交换链图像对应的呈现等待信号量。这里的参数是后续 Acquire 返回的 image index
        VulkanBinarySemaphore& GetPresentSemaphore(uint32_t imageIndex) { return *m_PresentSemaphores[imageIndex]; }

        // semaphore 必须已初始化、未触发，且不存在尚未完成的 signal/wait；timeout 单位为纳秒。
        [[nodiscard]] RHIStatus Acquire(VulkanBinarySemaphore& semaphore, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);

        // acquired 必须来自当前交换链，且尚未呈现。
        // 图像已转换到呈现布局，对应呈现信号量的 signal 提交已经成功。
        [[nodiscard]] RHIStatus Present(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome);

        // 请求后续重建，不立即销毁资源，也不覆盖 SurfaceLost 等状态。
        void RequestRecreate() { if (m_Info.state == RHISwapchainState::Ready) m_Info.state = RHISwapchainState::OutOfDate; }

    private:
        const VulkanDevice& m_Device;

        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;

        RHISwapchainInfo m_Info{};

        std::vector<std::unique_ptr<VulkanBinarySemaphore>> m_PresentSemaphores;
    };

}