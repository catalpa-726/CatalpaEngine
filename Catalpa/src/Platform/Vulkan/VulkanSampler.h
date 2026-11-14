#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "RHI/RHISamplerDesc.h"

namespace Catalpa {

	class VulkanDevice;

	/* 发布后由 Registry 管理，记录本身不自动销毁。 */
	struct VulkanSamplerRecord {
		VkSampler sampler = VK_NULL_HANDLE;
		bool linearFiltering = false;
	};

	/**
	 * @brief 根据 RHI 寻址、过滤和细节层级配置构造 Vulkan 采样器。
	 * Registry 已检查仍存活的 sampler 数量上限。
	 *
	 * @param[in] device 创建原生采样器的设备，并提供细节层级偏置上限。
	 * @param[in] desc 请求的纹理过滤、寻址模式、细节层级及比较采样配置。
	 * @param[out] record 输出原生采样器记录，进入函数时先清空。
	 *
	 * @return 返回原生采样器创建结果，描述不合法或底层创建失败时返回错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanSampler(
	    VulkanDevice& device, const RHISamplerDesc& desc, VulkanSamplerRecord& record);

	/**
	 * @brief 释放未发布或已允许退休的采样器，并清空原生记录。
	 * 仅用于未发布记录，或退休系统已确认允许销毁的记录。
	 *
	 * @param[in] device 创建该采样器的逻辑设备。
	 * @param[in,out] record 待销毁的原生采样器记录，调用结束后清空。
	 */
	void DestroyVulkanSampler(const VulkanDevice& device, VulkanSamplerRecord& record) noexcept;

}
