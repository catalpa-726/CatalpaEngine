#pragma once

#include "VulkanMemoryAllocator.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHIHandle.h"

/* 保存原生图像、VMA 内存分配、图像视图及父图像关系，由资源注册表管理生命周期。 */
namespace Catalpa {

	class VulkanDevice;

	/* 借用图像不接管原生对象及其内存。 */
	struct VulkanTextureRecord {
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		RHITextureDesc desc{};
		bool borrowed = false;
		bool presentable = false;
	};

	/* View 保留父纹理，命令引用则保留 View。 */
	struct VulkanTextureViewRecord {
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		Extent2D extent{};
		PixelFormat format = PixelFormat::Unknown;
		uint32_t sampleCount = 1;
		RHITextureUsage usage = RHITextureUsage::None;
		bool presentable = false;
		bool linearFiltering = false;
		TextureHandle texture{};
		bool borrowed = false;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
		RHITextureViewType type = RHITextureViewType::Texture2D;
	};

	/**
	 * @brief 核对纹理格式、子资源数和设备支持的用法，创建自有图像。
	 *
	 * @param[in] device 提供图像格式能力和内存分配器的设备。
	 * @param[in] desc 基级宽高（像素）、格式、用途、细节层级、数组层及采样数。
	 * @param[out] record 成功时接收图像、分配和描述，调用前须已释放原有资源。
	 *
	 * @return 图像与内存分配均就绪时成功，否则返回描述错误、能力限制或原生分配错误。
	 */
	[[nodiscard]] RHIStatus CreateVulkanTexture(
	    VulkanDevice& device, const RHITextureDesc& desc, VulkanTextureRecord& record);

	/**
	 * @brief Registry 已验证父句柄；这里检查 View 参数并创建原生视图。
	 *
	 * @param[in] device 拥有父图像、负责创建原生视图的逻辑设备。
	 * @param[in] texture 新视图所引用的父纹理句柄。
	 * @param[in] parent 父纹理的原生图像和描述，用于核对视图格式、类型及覆盖范围。
	 * @param[in] desc 视图类型、格式和子资源范围；格式为 Unknown 时沿用父纹理格式。
	 * @param[out] record 成功时接收原生视图及父图像信息，进入函数时先清空。
	 *
	 * @return 返回纹理视图描述检查和原生视图创建结果。
	 */
	[[nodiscard]] RHIStatus CreateVulkanTextureView(VulkanDevice& device, TextureHandle texture,
	    const VulkanTextureRecord& parent, const RHITextureViewDesc& desc, VulkanTextureViewRecord& record);

	/**
	 * @brief 仅接收未发布或已满足退休条件的记录；借用记录只清空自身。
	 *
	 * @param[in] device 持有纹理分配器的设备。
	 * @param[in,out] record 待清理的图像记录；自有图像连同分配一并销毁，借用图像只清空记录。
	 */
	void DestroyVulkanTexture(const VulkanDevice& device, VulkanTextureRecord& record) noexcept;
	/**
	 * @brief 释放自有原生视图；借用视图仅清空本地记录。
	 *
	 * @param[in] device 持有该原生视图的逻辑设备。
	 * @param[in,out] record 保存视图句柄和借用标记的记录，处理后恢复默认值。
	 */
	void DestroyVulkanTextureView(const VulkanDevice& device, VulkanTextureViewRecord& record) noexcept;

}
