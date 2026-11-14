#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "RHI/RHIBindingDesc.h"

namespace Catalpa {

	class VulkanDevice;

	class VulkanDescriptorAllocator {
	public:
		explicit VulkanDescriptorAllocator(const VulkanDevice& device) : m_Device(device) {}
		~VulkanDescriptorAllocator();

		VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
		VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;

		/**
		 * @brief layout 必须来自本设备，且满足 RHIMaxBindingsPerSet 及当前支持的描述符类型。
		 *
		 * @param[in] layout 由本设备创建的描述符集布局。
		 * @param[in] entries 布局实际使用的描述符条目。
		 * @param[out] descriptorSet 接收分配的描述符集。
		 *
		 * @return 返回描述符集分配状态；布局、容量或原生分配失败时返回错误。
		 */
		[[nodiscard]] RHIStatus Allocate(VkDescriptorSetLayout layout, std::span<const RHIBindingLayoutEntry> entries,
		    VkDescriptorSet& descriptorSet);

		/**
		 * @brief 在旧描述符集已结束使用后重置分配池，供后续命令复用。
		 * 调用前结束 GPU 使用，并重置或丢弃引用这些 set 的命令。
		 * 失败时调用方不得将命令列表恢复为可录制状态。
		 *
		 * @return 返回描述符池重置状态；失败时使用这些描述符的命令不可恢复录制。
		 */
		[[nodiscard]] RHIStatus Reset();

		/**
		 * @brief 释放分配器持有的全部描述符池，调用前须结束引用这些集合的 GPU 工作。
		 * 不等待 GPU；调用前满足与 Reset 相同的生命周期要求。
		 */
		void Shutdown();

	private:
		/*
		 * 池按真实layout的类型数量分配，而非为四种类型各预留32项/每set。
		 * 一个命令列表可覆盖完整262144-pass准入；pool仅按需增长并在帧槽完成后复用。
		 */
		static constexpr uint32_t SetsPerPool = 512;
		static constexpr uint32_t MaxSetsPerCommandList = 262144;
		static constexpr size_t MaxPools = 1024;

		struct Pool {
			VkDescriptorPool handle = VK_NULL_HANDLE;
			std::array<uint32_t, 4> counts{};
		};

		const VulkanDevice& m_Device;
		std::vector<Pool> m_Pools;
		/* 不同layout类型配比各自保留游标，交替draw不会丢弃尚有容量的其他类型池。 */
		std::map<std::array<uint32_t, 4>, size_t> m_CurrentPools;
		uint32_t m_AllocatedSets = 0;
	};

} /* 命名空间 Catalpa */
