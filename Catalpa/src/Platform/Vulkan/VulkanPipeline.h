#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIError.h"

namespace Catalpa {

	class VulkanDevice;

	/* 发布后由 Registry 独占管理，记录本身不自动销毁。 */
	struct VulkanPipelineRecord {
		bool compute = false;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		PixelFormat colorFormat = PixelFormat::Unknown;
		PixelFormat depthFormat = PixelFormat::Unknown;
		bool depthWrite = false;
		uint32_t sampleCount = 1;
		uint32_t vertexStride = 0;
		uint32_t vertexAttributeEnd = 0; /* 所有属性 offset + 字节数的最大值。 */
		uint32_t pushConstantBytes = 0;
		VkShaderStageFlags pushConstantStages = 0;
		VkDescriptorSetLayout bindingLayout = VK_NULL_HANDLE;
		std::vector<RHIBindingLayoutEntry> bindings;
	};

	/**
	 * @brief 核对着色器反射和设备限制，创建与动态渲染附件兼容的图形管线。
	 *
	 * @param[in] device 提供附件格式支持、物理设备限制和管线缓存的设备。
	 * @param[in] desc 顶点与片元入口、顶点格式、资源布局、附件格式和绘制状态。
	 * @param[out] pipeline 接收管线、布局及后续绘制校验所需的元数据；调用前须已释放原有资源。
	 *
	 * @return 成功时管线可供录制绘制，失败时返回首个布局、能力或原生创建错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanGraphicsPipeline(
	    VulkanDevice& device, const RHIGraphicsPipelineDesc& desc, VulkanPipelineRecord& pipeline);

	/**
	 * @brief 按计算着色器的反射布局创建描述符集布局、管线布局和计算管线。
	 *
	 * @param[in] device 提供计算能力、推送常量上限及管线缓存的逻辑设备。
	 * @param[in] desc 计算入口及其声明的绑定与推送常量范围。
	 * @param[out] output 成功后接收完整管线及布局；失败时保留调用前的值。
	 *
	 * @return 设备支持计算且布局和原生管线均创建成功时返回成功，否则返回对应阶段的错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanComputePipeline(
	    VulkanDevice&, const RHIComputePipelineDesc&, VulkanPipelineRecord&);

	/**
	 * @brief 按依赖关系释放管线及布局，并清空已退休或创建失败的原生记录。
	 * 用于创建失败的未发布记录，或退休系统已经允许销毁的记录。
	 *
	 * @param[in] device 拥有管线及其布局的逻辑设备。
	 * @param[in,out] pipeline 持有管线、管线布局及描述符集布局的记录，销毁后清空。
	 */
	void DestroyVulkanPipeline(const VulkanDevice& device, VulkanPipelineRecord& pipeline) noexcept;

}