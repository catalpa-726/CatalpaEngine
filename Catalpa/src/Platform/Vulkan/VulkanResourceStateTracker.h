#pragma once

#include "RHI/RHIError.h"
#include "RHI/RHIResourceState.h"

// 统一查询和校验状态，并在提交被接受后发布更新
namespace Catalpa {

    class VulkanResourceRegistry;

    class VulkanResourceStateTracker {
    public:
        // 取得独立快照，图编译不会借用或修改槽位中的状态。
        [[nodiscard]] static RHIStatus Query(const VulkanResourceRegistry& registry, BufferHandle buffer, RHIBufferStateSnapshot& snapshot);
        [[nodiscard]] static RHIStatus Query(const VulkanResourceRegistry& registry, TextureViewHandle view, RHITextureStateSnapshot& snapshot);

        // 核对句柄、版本、范围与访问；提交前还要求资源已由命令保留。
        [[nodiscard]] static RHIStatus Validate(
            const VulkanResourceRegistry& registry,
            std::span<const RHIBufferStateUpdate> buffers,
            std::span<const RHITextureStateUpdate> textures,
            bool requireRetained) noexcept;

        // 仅在原生提交成功后调用一次；交换已有状态，不再分配内存。
        static void CommitAccepted(
            VulkanResourceRegistry& registry,
            std::span<RHIBufferStateUpdate> buffers,
            std::span<RHITextureStateUpdate> textures) noexcept;
    };

}