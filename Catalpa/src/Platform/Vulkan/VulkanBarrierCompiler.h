#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandTypes.h"
#include "RHI/RHIError.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

	class VulkanResourceRegistry;

	/**
	 * @brief 当前供图形队列使用；调用者提供正确的前后访问状态。
	 *
	 * @param[in] desc 纹理访问阶段、布局及队列所有权转换。
	 * @param[in] registry 用于解析纹理视图及父图像的资源注册表。
	 * @param[out] barrier 接收可提交的 Vulkan 图像屏障。
	 *
	 * @return 返回图像屏障的编译状态；纹理、访问或所有权描述不合法时返回错误。
	 */
	[[nodiscard]] RHIStatus CompileVulkanTextureBarrier(
	    const RHITextureBarrier& desc, const VulkanResourceRegistry& registry, VkImageMemoryBarrier2& barrier) noexcept;

	/**
	 * @brief 从资源注册表解析缓冲区并编译 Vulkan 同步屏障。
	 *
	 * @param[in] desc 缓冲区访问和所有权转换描述。
	 * @param[in] registry 用于解析缓冲区句柄的资源注册表。
	 * @param[out] barrier 接收可提交的 Vulkan 缓冲区屏障。
	 *
	 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
	 */
	[[nodiscard]] RHIStatus CompileVulkanBufferBarrier(
	    const RHIBufferBarrier& desc, const VulkanResourceRegistry& registry, VkBufferMemoryBarrier2& barrier) noexcept;

	/**
	 * @brief 共享 Buffer 访问转换与合法性检查，供屏障编码和状态提交校验复用。
	 *
	 * @param[in] source 要转换的缓冲区流水线阶段和访问权限。
	 * @param[in] usage 缓冲区创建时指定的用途。
	 * @param[out] stage 接收流水线阶段掩码。
	 * @param[out] access 接收访问掩码。
	 *
	 * @return 缓冲区用途允许该访问且可生成 Vulkan 同步掩码时返回 true。
	 */
	[[nodiscard]] bool ConvertVulkanBufferAccess(const RHIBufferAccess& source, RHIBufferUsage usage,
	    VkPipelineStageFlags2& stage, VkAccessFlags2& access) noexcept;

	/**
	 * @brief 仅转换调用者已声明的访问，未声明的资源依赖由调用方补齐。
	 *
	 * @param[in] source 纹理访问和抽象布局声明。
	 * @param[in] usage 纹理创建时指定的用途。
	 * @param[in] presentable 是否允许将此图像用于呈现。
	 * @param[out] stage 接收流水线阶段掩码。
	 * @param[out] access 接收访问掩码。
	 * @param[out] layout 接收图像布局。
	 *
	 * @return 纹理用途支持所声明的访问，且可转换成 Vulkan 阶段、访问掩码和布局时返回 true。
	 */
	[[nodiscard]] bool ConvertVulkanTextureAccess(const RHITextureAccess& source, RHITextureUsage usage,
	    bool presentable, VkPipelineStageFlags2& stage, VkAccessFlags2& access, VkImageLayout& layout) noexcept;

}