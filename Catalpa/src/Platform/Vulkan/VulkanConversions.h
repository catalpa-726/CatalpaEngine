#pragma once

#include "VulkanHeaders.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"
#include "RHI/RHIRenderingDesc.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHIShaderDesc.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

	/**
	 * @brief 将引擎像素格式映射为使用相同编码的 Vulkan 格式。
	 *
	 * @param[in] format 纹理或附件使用的引擎像素格式，包括通道排列、数值类型和颜色编码。
	 *
	 * @return 返回对应的 VkFormat；未知或不受支持的像素格式返回 VK_FORMAT_UNDEFINED。
	 */
	[[nodiscard]] VkFormat ToVulkanFormat(PixelFormat format) noexcept;

	/**
	 * @brief 将附件开始渲染时的加载策略映射为 Vulkan 操作。
	 *
	 * @param[in] operation 保留既有附件内容、清除附件或丢弃旧内容的策略。
	 *
	 * @return 返回对应的 Vulkan 加载操作；无法识别的策略返回 VK_ATTACHMENT_LOAD_OP_MAX_ENUM。
	 */
	[[nodiscard]] VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept;
	/**
	 * @brief 将附件结束渲染时的存储策略映射为 Vulkan 操作。
	 *
	 * @param[in] operation 渲染段结束时保留或丢弃附件内容的策略。
	 *
	 * @return 返回对应的 Vulkan 存储操作；无法识别的策略返回 VK_ATTACHMENT_STORE_OP_MAX_ENUM。
	 */
	[[nodiscard]] VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept;

	/**
	 * @brief 将引擎的缓冲区用途组合转换为 Vulkan 创建标志。
	 *
	 * @param[in] usage 创建缓冲区时声明的顶点、索引、存储、复制等用途位。
	 *
	 * @return 返回对应的 VkBufferUsageFlags；输入含未知用途位时返回 0。
	 */
	[[nodiscard]] VkBufferUsageFlags ToVulkanBufferUsage(RHIBufferUsage usage) noexcept;

	/**
	 * @brief 转换纹理用途；包含未知用途位时返回 0。
	 *
	 * @param[in] usage 创建纹理时申请的引擎用途位组合；含未知位时整个转换返回 0。
	 *
	 * @return 返回相应 Vulkan 用途位组合；输入含未知位时返回 0。
	 */
	[[nodiscard]] VkImageUsageFlags ToVulkanTextureUsage(RHITextureUsage usage) noexcept;

	/**
	 * @brief 将顶点属性的分量类型和数量映射为 Vulkan 格式。
	 *
	 * @param[in] format 单个顶点属性的数据格式，如两个或三个单精度浮点分量。
	 *
	 * @return 返回顶点输入所需的 VkFormat；不支持的顶点属性格式返回 VK_FORMAT_UNDEFINED。
	 */
	[[nodiscard]] VkFormat ToVulkanFormat(VertexFormat format) noexcept;

	/**
	 * @brief 将引擎的参数可见范围转换为 Vulkan 着色阶段集合。
	 *
	 * @param[in] visibility 使用该绑定的着色阶段，可为顶点、片元、顶点与片元或计算阶段。
	 *
	 * @return 返回可见阶段的 Vulkan 位组合；不支持的组合返回 0。
	 */
	[[nodiscard]] VkShaderStageFlags ToVulkanShaderStages(RHIShaderVisibility visibility) noexcept;

}