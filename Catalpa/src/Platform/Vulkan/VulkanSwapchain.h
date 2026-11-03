#pragma once

#include "RHI/RHISwapchain.h"
#include "RHI/RHIError.h"
#include "VulkanHeaders.h"

// 持有 Vulkan 交换链及其图像视图，图像本身是从交换链借用的
namespace Catalpa {

    class VulkanDevice;
    class VulkanBinarySemaphore;
    class VulkanFrameContext;
    class VulkanFence;

    class VulkanSwapchain : public RHISwapchain {
    public:
        // 构造与所有权
        VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface);
        ~VulkanSwapchain() override;

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        // 创建与释放；同步收尾由拥有者在调用前完成
        [[nodiscard]] RHIStatus Init(const RHISwapchainDesc& desc);
        void Shutdown() override;

        // 当前配置与原生对象
        const RHISwapchainInfo& GetInfo() const override { return m_Info; }
        VkSwapchainKHR GetHandle() const { return m_Swapchain; }

        // 供后端按有效图像索引取得图像及视图。
        VkImage GetImage(uint32_t index) const { return m_Images[index]; }
        VkImageView GetImageView(uint32_t index) const { return m_ImageViews[index]; }
        TextureViewHandle GetImageViewHandle(uint32_t imageIndex) const override { return m_ImageViewHandles[imageIndex]; }

        // 重建前完成当前同步策略，并释放录制命令保留的引用。
        [[nodiscard]] RHIStatus Recreate(const RHISwapchainDesc& desc) override;

        // 取得 Acquire 返回的图像索引所对应的呈现等待信号量。
        VulkanBinarySemaphore& GetPresentSemaphore(uint32_t imageIndex) { return *m_PresentSemaphores[imageIndex]; }

        // frame 必须来自已准备的帧槽；其 Acquire 信号量须可用且没有未完成的 signal/wait，timeout 单位为纳秒。
        [[nodiscard]] RHIStatus Acquire(RHIFrameToken frame, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired) override;

        // 提交同步操作以消耗尚未提交帧的 Acquire 信号量，并请求重建。
        [[nodiscard]] RHIStatus CancelAcquire(RHIAcquireResult& acquired, RHICompletionPoint& completion) override;

        // acquired 必须来自当前交换链，且尚未呈现。
        // 图像已转换到呈现布局，对应呈现信号量的 signal 提交已经成功。
        [[nodiscard]] RHIStatus Present(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome) override;

        // 等待已启用的呈现 Fence；无此能力时返回 Unsupported。
        [[nodiscard]] RHIStatus WaitForPresentCompletion() override;

        // 请求后续重建，不立即销毁资源，也不覆盖 SurfaceLost 等状态。
        void RequestRecreate() { if (m_Info.state == RHISwapchainState::Ready) m_Info.state = RHISwapchainState::OutOfDate; }

    private:
        // 帧上下文负责校验获取与提交身份，再调用原生入口。
        friend class VulkanFrameContext;

        [[nodiscard]] RHIStatus AcquireNative(VulkanBinarySemaphore& semaphore, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);
        [[nodiscard]] RHIStatus PresentNative(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome);

    private:
        // 设备和表面为借用，交换链句柄由本对象销毁。
        VulkanDevice& m_Device;

        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

        // 图像为借用，视图自有；RHI 句柄登记在设备中。
        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;
        std::vector<TextureViewHandle> m_ImageViewHandles;

        RHISwapchainInfo m_Info{};

        // 按图像持有呈现同步对象；仅启用 presentFence 时创建 Fence。
        std::vector<std::unique_ptr<VulkanBinarySemaphore>> m_PresentSemaphores;
        std::vector<std::unique_ptr<VulkanFence>> m_PresentFences;
    };

}