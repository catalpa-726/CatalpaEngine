#pragma once

#include "VulkanMemoryAllocator.h"
#include "RHI/RHIBufferDesc.h"

/* 定义后端 Buffer 记录及创建、写入、销毁接口 */
namespace Catalpa {

	class VulkanDevice;

	/* Registry 独占管理原生载荷；记录本身不自动销毁。 */
	struct VulkanBufferRecord {
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		RHIBufferDesc desc{};
		std::byte* mapped = nullptr;
	};

	/**
	 * @brief 为缓冲区分配内存，上传域和读回域同时建立主机映射。
	 *
	 * @param[in] device 提供 VMA 分配器的逻辑设备。
	 * @param[in] desc 缓冲容量（字节）、用途和内存域；读回缓冲须支持传输写入。
	 * @param[out] record 接收原生缓冲、VMA 分配及映射地址，调用前须已释放原有资源。
	 *
	 * @return 描述有效且所需内存、映射均就绪时成功，否则返回参数、分配或映射错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanBuffer(
	    VulkanDevice& device, const RHIBufferDesc& desc, VulkanBufferRecord& record);

	/**
	 * @brief 将数据复制到已验证的主机可写缓冲区并刷新对应内存范围。
	 * 仅由 Registry 在验证 Upload 映射、写入范围和非空数据后调用。
	 *
	 * @param[in] device 持有该缓冲区分配器的逻辑设备。
	 * @param[in] record 已映射的目标缓冲区及其分配记录，调用前须验证写入范围。
	 * @param[in] offset 写入起点相对该分配首字节的偏移，不包含底层设备内存偏移。
	 * @param[in] data 要复制到映射区并刷新的源字节。
	 *
	 * @return 返回映射数据写入及刷新状态；失败时可从状态取得底层操作和错误。
	 */
	[[nodiscard]] RHIStatus WriteVulkanBuffer(
	    const VulkanDevice& device, const VulkanBufferRecord& record, uint64_t offset, std::span<const std::byte> data);

	/**
	 * @brief 销毁底层缓冲区并清空记录，适用于未发布的候选或已退休资源。
	 * 仅用于尚未发布的失败记录，或退休系统已确认可销毁的记录。
	 *
	 * @param[in] device 创建该缓冲区的设备，提供对应的内存分配器。
	 * @param[in,out] record 持有原生缓冲区及分配句柄的记录，销毁后清空。
	 */
	void DestroyVulkanBuffer(const VulkanDevice& device, VulkanBufferRecord& record) noexcept;

}