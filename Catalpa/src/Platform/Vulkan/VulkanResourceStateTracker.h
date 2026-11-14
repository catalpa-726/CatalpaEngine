#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHIResourceState.h"

/* 统一查询和校验状态，并在提交被接受后发布更新 */
namespace Catalpa {

	class VulkanResourceRegistry;

	class VulkanResourceStateTracker {
	public:
		/**
		 * @brief 取得独立快照，图编译不会借用或修改槽位中的状态。
		 *
		 * @param[in] registry 持有资源和变更序号的注册表。
		 * @param[in] buffer 要查询的资源句柄。
		 * @param[out] snapshot 输出资源描述及最近接受的状态。
		 *
		 * @return 返回查询设备中缓冲区当前状态的结果，成功时输出独立快照。
		 */
		[[nodiscard]] static RHIStatus Query(
		    const VulkanResourceRegistry& registry, BufferHandle buffer, RHIBufferStateSnapshot& snapshot);
		/**
		 * @brief 核对资源身份后返回当前已接受的资源状态快照。
		 *
		 * @param[in] registry 持有资源和变更序号的注册表。
		 * @param[in] view 要查询的资源句柄。
		 * @param[out] snapshot 输出资源描述及最近接受的状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] static RHIStatus Query(
		    const VulkanResourceRegistry& registry, TextureViewHandle view, RHITextureStateSnapshot& snapshot);

		/**
		 * @brief false：对尚未绑定的计划做完整校验。
		 * true：只复核提交时资源；计划须已完整校验，并由命令独占保存且未经修改。
		 *
		 * @param[in] registry 提供缓冲区和纹理状态的资源注册表。
		 * @param[in] buffers 缓冲区状态更新集合。
		 * @param[in] textures 纹理状态更新集合。
		 * @param[in] requireRetained 为真时按提交阶段检查命令引用及当前版本。
		 *
		 * @return 返回设备、版本、引用及资源状态是否满足更新前提的校验结果。
		 */
		[[nodiscard]] static RHIStatus Validate(const VulkanResourceRegistry& registry,
		    std::span<const RHIBufferStateUpdate> buffers, std::span<const RHITextureStateUpdate> textures,
		    bool requireRetained) noexcept;

		/**
		 * @brief 仅在原生提交成功后调用一次；交换已有状态，不再分配内存。
		 *
		 * @param[in,out] registry 接收新状态的注册表。
		 * @param[in,out] buffers 已验证的缓冲区更新，内容将交换到注册表。
		 * @param[in,out] textures 已验证的纹理更新，内容将交换到注册表。
		 * @param[in] completion 提交所属队列的完成点。
		 */
		static void CommitAccepted(VulkanResourceRegistry& registry, std::span<RHIBufferStateUpdate> buffers,
		    std::span<RHITextureStateUpdate> textures, RHICompletionPoint completion) noexcept;
	};

}
