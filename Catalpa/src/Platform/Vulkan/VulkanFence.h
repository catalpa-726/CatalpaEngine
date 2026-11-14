#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

/* 声明 Fence 的所有权、队列操作状态和 CPU 等待接口。原生句柄只供 Vulkan 后端使用。 */
namespace Catalpa {

	class VulkanDevice;

	enum class VulkanFenceState {
		Unsubmitted, /* 未置位，尚未登记关联队列操作 */
		Pending, /* 相关队列操作已入队，尚未观察到完成 */
		Signaled /* 已置位；可能来自创建参数，也可能来自关联队列操作完成 */
	};

	class VulkanFence {
	public:
		explicit VulkanFence(const VulkanDevice& device);
		~VulkanFence();

		VulkanFence(const VulkanFence&) = delete;
		VulkanFence& operator=(const VulkanFence&) = delete;

		/**
		 * @brief 创建原生栅栏并登记初始信号状态。
		 * 创建 Fence，成功后才保存句柄
		 *
		 * @param[in] signaled 为真时创建为已触发状态。
		 *
		 * @return 返回原生栅栏创建结果，成功时才保存句柄。
		 */
		[[nodiscard]] RHIStatus Init(bool signaled = false);
		/**
		 * @brief 等待已登记的队列操作；通过 signaled 返回是否已置位
		 *
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] signaled 接收是否已观测到栅栏信号。
		 *
		 * @return 返回等待操作的状态；等待超时与栅栏已触发由 signaled 区分。
		 */
		[[nodiscard]] RHIStatus Wait(uint64_t timeoutNanoseconds, bool& signaled);
		/**
		 * @brief 在确认前次队列操作完成后清除栅栏信号，使其可用于下一次提交。
		 * 为下一次队列操作清除信号，拒绝重置尚未观察到完成的 Fence
		 *
		 * @return 返回栅栏重置结果；未确认待处理操作已完成时返回错误。
		 */
		[[nodiscard]] RHIStatus Reset();

		/**
		 * @brief 将已经被队列接受的同步操作登记为栅栏待完成状态。
		 * 仅在相关提交或呈现操作已被队列接受后登记 Pending
		 */
		void MarkSubmitted();

		/**
		 * @brief 释放原生栅栏，调用方须预先结束仍使用它的操作。
		 * 调用前，拥有者必须结束所有引用此 Fence 的操作。
		 */
		void Shutdown();

		VkFence GetHandle() const { return m_Fence; }
		VulkanFenceState GetState() const { return m_State; }

	private:
		const VulkanDevice& m_Device;
		VkFence m_Fence = VK_NULL_HANDLE;
		VulkanFenceState m_State = VulkanFenceState::Unsubmitted;
	};

}