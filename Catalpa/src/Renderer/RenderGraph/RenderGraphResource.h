#pragma once

#include "Core/Memory/GenerationalHandle.h"

// 定义图内资源身份，复用已有 GenerationalHandle。图内句柄与设备资源句柄分别管理，避免跨图误用
namespace Catalpa {

    template<typename Tag>
    struct RGHandle {
        uint64_t graph = 0;
        GenerationalHandle<Tag> slot{};

        // 这里只检查身份是否有值，归属和索引由 RenderGraph 检查。
        [[nodiscard]] constexpr bool HasValue() const noexcept { return graph != 0 && slot.HasValue(); }
        constexpr bool operator==(const RGHandle&) const = default;
    };

    struct RGBufferTag;
    struct RGTextureTag;

    using RGBuffer = RGHandle<RGBufferTag>;
    using RGTexture = RGHandle<RGTextureTag>;

}