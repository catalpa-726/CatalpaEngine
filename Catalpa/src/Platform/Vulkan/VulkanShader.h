#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIShaderDesc.h"
#include "RHI/RHIError.h"

namespace Catalpa {

	class VulkanDevice;

	/* 只用于同步构建管线，不发布给上层。 */
	struct VulkanShaderRecord {
		VkShaderModule module = VK_NULL_HANDLE;
		VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
		const char* entryPoint = nullptr;
	};

	/**
	 * @brief 验证着色器资产的入口与阶段，再用其 SPIR-V 字节码创建临时模块。
	 *
	 * @param[in] device 用于创建模块的逻辑设备。
	 * @param[in] desc 固定版本的着色器资产、目标阶段和入口名。
	 * @param[out] shader 接收模块、阶段及借用的入口名指针；入口名须存活至管线创建结束。
	 *
	 * @return 资产与阶段有效且 Vulkan 模块创建成功时返回成功，失败时携带相应校验或创建错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanShader(
	    VulkanDevice& device, const RHIShaderDesc& desc, VulkanShaderRecord& shader);
	/**
	 * @brief 在管线创建结束后释放临时着色器模块并清空记录。
	 *
	 * @param[in] device 创建此着色器模块的逻辑设备。
	 * @param[in,out] shader 持有模块的记录；空记录也可传入，返回后恢复默认值。
	 */
	void DestroyVulkanShader(const VulkanDevice& device, VulkanShaderRecord& shader) noexcept;

}