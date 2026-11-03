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

        // false：对尚未绑定的计划做完整校验。
        // true：只复核提交时资源；计划须已完整校验，并由命令独占保存且未经修改。
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