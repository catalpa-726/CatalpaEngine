#pragma once

#include "RHI/RHISwapchain.h"
#include "RHI/RHIError.h"
#include "VulkanHeaders.h"

/* 持有 Vulkan 交换链及其图像视图，图像本身是从交换链借用的 */
namespace Catalpa {

	class VulkanDevice;
	class VulkanSurface;
	class VulkanBinarySemaphore;
	class VulkanFrameContext;
	class VulkanFence;

	class VulkanSwapchain : public RHISwapchain {
	public:
		VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface);
		VulkanSwapchain(VulkanDevice&, std::unique_ptr<VulkanSurface> surface);
		~VulkanSwapchain() override;

		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

		/**
		 * @brief 查询表面能力，选择支持的格式与呈现模式，并建立交换链图像及视图。
		 * 查询表面能力并按实际支持的格式创建交换链、图像视图和同步对象。尺寸为空时进入挂起状态。
		 *
		 * @param[in] desc 请求的尺寸、呈现和图像格式配置。
		 *
		 * @return 返回交换链及图像资源的初始化状态。
		 */
		[[nodiscard]] RHIStatus Init(const RHISwapchainDesc& desc);
		/**
		 * @brief 释放交换链的图像视图、同步对象和呈现资源。
		 */
		void Shutdown() override;

		const RHISwapchainInfo& GetInfo() const override { return m_Info; }
		VkSwapchainKHR GetHandle() const { return m_Swapchain; }

		VkImage GetImage(uint32_t index) const { return m_Images[index]; }
		VkImageView GetImageView(uint32_t index) const { return m_ImageViews[index]; }
		TextureViewHandle GetImageViewHandle(uint32_t imageIndex) const override {
			return m_ImageViewHandles[imageIndex];
		}

		/**
		 * @brief 调用前须完成旧交换链的 GPU 同步并释放命令引用。重建失败时清理本次创建的资源。
		 *
		 * @param[in] desc 期望的图像尺寸、格式和呈现配置。
		 *
		 * @return 返回在已有同步条件下重建交换链及其图像的状态。
		 */
		[[nodiscard]] RHIStatus Recreate(const RHISwapchainDesc& desc) override;

		VulkanBinarySemaphore& GetPresentSemaphore(uint32_t imageIndex) { return *m_PresentSemaphores[imageIndex]; }

		/**
		 * @brief 使用已准备的帧槽获取下一张图像。该槽用于获取图像的信号量必须可用，且没有尚未完成的等待或发信操作。
		 *
		 * @param[in] frame 调用方已准备的帧槽令牌。
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] acquired 接收视图、图像索引和本次同步标识。
		 *
		 * @return 返回获取操作状态，图像可用性与交换链变化由 acquired 的结果字段给出。
		 */
		[[nodiscard]] RHIStatus Acquire(
		    RHIFrameToken frame, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired) override;

		/**
		 * @brief 通过一次队列提交消耗已获取图像的信号量，并标记交换链需要重建。回收帧资源前须等待返回的完成点。
		 *
		 * @param[in,out] acquired 要收尾的图像及其获取凭证，取消完成后清空。
		 * @param[out] completion 成功时返回图形队列此次收尾操作的完成点，调用开始时置空。
		 *
		 * @return 返回取消获取的提交结果；成功时用 completion 等待信号量收尾操作完成。
		 */
		[[nodiscard]] RHIStatus CancelAcquire(RHIAcquireResult& acquired, RHICompletionPoint& completion) override;

		/**
		 * @brief 将当前交换链中已获取的图像交给呈现队列。调用前应完成呈现布局转换，并已成功提交对呈现信号量的发信操作。
		 *
		 * @param[in] acquired 先前取得的交换链图像及获取标识。
		 * @param[out] outcome 输出呈现是否已进入队列，调用开始时设为 NotQueued。
		 *
		 * @return 返回呈现调用的状态，具体呈现结果另写入 outcome。
		 */
		[[nodiscard]] RHIStatus Present(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome) override;

		/**
		 * @brief 等待所有已提交的呈现栅栏。不支持呈现栅栏时返回 Unsupported。
		 *
		 * @return 返回全部已用呈现栅栏的等待状态；设备未提供该能力时返回 Unsupported。
		 */
		[[nodiscard]] RHIStatus WaitForPresentCompletion() override;

		void RequestRecreate() {
			if (m_Info.state == RHISwapchainState::Ready)
				m_Info.state = RHISwapchainState::OutOfDate;
		}

	private:
		/* 帧上下文负责校验获取与提交身份，再调用原生入口。 */
		friend class VulkanFrameContext;

		[[nodiscard]] RHIStatus AcquireNative(
		    VulkanBinarySemaphore& semaphore, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);
		[[nodiscard]] RHIStatus PresentNative(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome);

	private:
		/* 设备和表面为借用，交换链句柄由本对象销毁。 */
		VulkanDevice& m_Device;

		std::unique_ptr<VulkanSurface> m_OwnedSurface; /* 仅附加窗口由本交换链持有表面。 */
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

		/* 图像为借用，视图自有；RHI 句柄登记在设备中。 */
		std::vector<VkImage> m_Images;
		std::vector<VkImageView> m_ImageViews;
		/* 按当前交换链的图像索引保存独立的视图句柄。 */
		std::vector<TextureViewHandle> m_ImageViewHandles;

		RHISwapchainInfo m_Info{};

		/* 按图像持有呈现同步对象；仅启用 presentFence 时创建 Fence。 */
		std::vector<std::unique_ptr<VulkanBinarySemaphore>> m_PresentSemaphores;
		std::vector<std::unique_ptr<VulkanFence>> m_PresentFences;
	};

}