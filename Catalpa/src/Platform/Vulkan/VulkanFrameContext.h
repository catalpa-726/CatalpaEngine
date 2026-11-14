#pragma once

#include "VulkanCommandPool.h"
#include "VulkanFence.h"
#include "VulkanBinarySemaphore.h"
#include "VulkanCommandList.h"
#include "RHI/RHISwapchainDesc.h"

/* 集中持有一个帧槽的命令资源、提交 Fence 和 Acquire 信号量 */
namespace Catalpa {

	class VulkanDevice;
	class VulkanQueue;
	class VulkanSwapchain;

	class VulkanFrameContext {
	public:
		VulkanFrameContext(VulkanDevice& device, uint32_t slot);
		/**
		 * @brief 先销毁原生命令池，随后成员析构归还命令保留的资源引用。
		 */
		~VulkanFrameContext();

		VulkanFrameContext(const VulkanFrameContext&) = delete;
		VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;

		/**
		 * @brief 为当前队列族创建帧槽独占的命令池、命令列表及提交同步资源。
		 * 初始化与帧槽复用，等待时间以纳秒为单位。
		 *
		 * @param[in] queueFamily 图形队列所在的队列族索引。
		 *
		 * @return 返回帧槽命令及同步资源的初始化结果。
		 */
		[[nodiscard]] RHIStatus Init(uint32_t queueFamily);
		/**
		 * @brief 等待之前的提交完成并准备复用本上下文的命令资源。
		 *
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] ready 接收上下文是否已经可以复用。
		 *
		 * @return 返回帧槽等待及重置结果；仍在等待时 ready 为 false。
		 */
		[[nodiscard]] RHIStatus PrepareReuse(uint64_t timeoutNanoseconds, bool& ready);

		RHIFrameToken GetToken() const;
		bool HasActiveAcquisition() const { return m_ActiveSwapchain != nullptr; }

		VkCommandBuffer GetCommandBuffer() const { return m_CommandList.GetHandle(); }
		VulkanCommandList& GetCommandList() { return m_CommandList; }
		VulkanFence& GetFence() { return m_Fence; }
		VulkanBinarySemaphore& GetAcquireSemaphore() { return m_AcquireSemaphore; }

		/**
		 * @brief 通过帧槽的获取信号量从交换链取得图像。
		 *
		 * @param[in,out] swapchain 提供图像的交换链。
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] acquired 接收图像索引、视图和同步依赖。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Acquire(
		    VulkanSwapchain& swapchain, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);
		/**
		 * @brief 为已经获取图像的帧槽提交录制命令并关联同步标识。
		 *
		 * @param[in] queue 执行渲染的队列。
		 * @param[in] wait 本帧图像已获取的等待依赖。
		 * @param[in] signal 本帧允许呈现的完成信号。
		 * @param[in] dependencies 本次提交必须等待的队列完成点序列。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Submit(VulkanQueue& queue, const RHIGpuDependency& wait, const RHIGpuDependency& signal,
		    std::span<const RHICompletionPoint> dependencies = {});

		/**
		 * @brief 检查提交与获取对应关系，再进入原生呈现。
		 *
		 * @param[in,out] swapchain 持有目标图像的交换链。
		 * @param[in] acquired 成功获取该图像时产生的结果。
		 * @param[out] outcome 输出图像是否已进入呈现队列，用于决定后续同步资源如何收尾。
		 *
		 * @return 返回提交核对和呈现调用的状态；outcome 单独指示操作是否进入呈现队列等结果。
		 */
		[[nodiscard]] RHIStatus Present(
		    VulkanSwapchain& swapchain, const RHIAcquireResult& acquired, RHIPresentOutcome& outcome);

		/**
		 * @brief 提交等待以消耗未提交帧的 Acquire 信号量，图像留待交换链重建时释放。
		 *
		 * @param[in,out] queue 用于提交同步操作的队列。
		 * @param[in,out] swapchain 本次获取所属的交换链。
		 * @param[in,out] acquired 待取消的已获取图像及其同步标识，完成收尾后清空。
		 *
		 * @return 返回收尾提交状态；成功时获取信号量已安排消耗，图像随后随交换链重建回收。
		 */
		[[nodiscard]] RHIStatus CancelAcquire(
		    VulkanQueue& queue, VulkanSwapchain& swapchain, RHIAcquireResult& acquired);

	private:
		/* 帧状态与身份 */
		enum class State { Available, Acquired, Submitted };

		VulkanDevice& m_Device;
		uint32_t m_Slot;
		uint64_t m_Generation = 0;
		State m_State = State::Available;

		/* 尚未交给呈现或取消提交的获取 */
		VulkanSwapchain* m_ActiveSwapchain = nullptr;
		RHIAcquireResult m_Acquired{};

		/* 析构体先销毁原生命令池，随后命令列表成员归还资源引用。 */
		VulkanFence m_Fence;
		VulkanBinarySemaphore m_AcquireSemaphore;
		VulkanCommandPool m_CommandPool;
		VulkanCommandList m_CommandList;
	};

} /* 命名空间 Catalpa */
