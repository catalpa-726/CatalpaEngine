#pragma once
#include "VulkanCommandPool.h"
#include "VulkanCommandList.h"
#include "VulkanFence.h"

namespace Catalpa {
	/* 每个批次独占命令池和描述符池，仅在该批次的栅栏完成后重置。 */
	class VulkanAuxiliaryContext final {
	public:
		VulkanAuxiliaryContext(VulkanDevice&, QueueId);
		~VulkanAuxiliaryContext();
		/**
		 * @brief 为辅助队列创建命令池、主命令列表及初始已触发的栅栏。
		 *
		 * @param[in] family 辅助命令池的队列族。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Init(uint32_t family);
		/**
		 * @brief 等待之前的提交完成并准备复用本上下文的命令资源。
		 *
		 * @param[in] timeout 等待上一次辅助提交完成的最长时间，单位为纳秒。
		 * @param[out] ready 输出是否已完成等待并重置命令资源；超时为 false。
		 *
		 * @return 返回等待及重置命令资源的状态；超时可成功返回，此时 ready 为 false。
		 */
		RHIStatus PrepareReuse(uint64_t timeout, bool& ready);
		VulkanCommandList& Commands() noexcept { return m_Commands; }
		VulkanFence& Fence() noexcept { return m_Fence; }
		QueueId Queue() const noexcept { return m_Queue; }
		uint64_t generation = 0;

	private:
		VulkanDevice& m_Device;
		QueueId m_Queue;
		VulkanFence m_Fence;
		VulkanCommandPool m_Pool;
		VulkanCommandList m_Commands;
	};
} /* 命名空间 Catalpa */
