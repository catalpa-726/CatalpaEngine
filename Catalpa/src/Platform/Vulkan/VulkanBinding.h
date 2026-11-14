#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIBindingDesc.h"
#include "RHI/RHIResourceState.h"
#include "RHI/RHICapabilities.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	/**
	 * @brief 创建前校验唯一set0布局及每stage/set上限；输出只借用，owner仍为PipelineRecord。
	 *
	 * @param[in] entries 管线声明的绑定项，每项包含唯一的绑定编号、描述符类型和可见阶段。
	 * @param[in] limits 物理设备报告的描述符数量及着色阶段资源上限。
	 * @param[in] colorAttachmentCount 片元阶段使用的颜色附件数，计入该阶段的资源总量。
	 * @param[out] output 按输入条目顺序接收原生布局绑定，容量至少为 entries 的元素数。
	 *
	 * @return 返回布局编译状态；绑定冲突、描述符数量超限或输出空间不足时返回错误。
	 */
	[[nodiscard]] RHIStatus BuildVulkanBindingLayout(std::span<const RHIBindingLayoutEntry> entries,
	    const VkPhysicalDeviceLimits& limits, uint32_t colorAttachmentCount,
	    std::span<VkDescriptorSetLayoutBinding> output) noexcept;
	/**
	 * @brief 检查统一缓冲的类型、对齐、范围与初始化，不创建或持有另一份资源。
	 *
	 * @param[in] binding 待校验的统一缓冲区绑定，offset 和 size 以字节计。
	 * @param[in] snapshot 目标缓冲区的用途、容量和已初始化区间。
	 * @param[in] capabilities 提供统一缓冲区偏移对齐要求与单次绑定的最大字节数。
	 *
	 * @return 返回统一缓冲绑定的校验结果，包括用途、有效读取范围和设备偏移对齐要求。
	 */
	[[nodiscard]] RHIStatus ValidateVulkanUniformBinding(const RHIBinding& binding,
	    const RHIBufferStateSnapshot& snapshot, const RHICapabilities& capabilities) noexcept;
}
