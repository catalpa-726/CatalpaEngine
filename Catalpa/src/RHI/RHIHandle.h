#pragma once

#include "Core/Memory/GenerationalHandle.h"
#include "RHITypes.h"

// 通用槽位句柄外增加设备身份，并区分纹理和纹理视图
namespace Catalpa {

    template<typename Tag>
    struct RHIHandle {
        DeviceId device{};
        GenerationalHandle<Tag> slot{};

        [[nodiscard]] constexpr bool HasValue() const noexcept { return device.HasValue() && slot.HasValue(); }
        constexpr bool operator==(const RHIHandle&) const = default;
    };

    struct RHITextureTag;
    struct RHITextureViewTag;

    using TextureHandle = RHIHandle<RHITextureTag>;
    using TextureViewHandle = RHIHandle<RHITextureViewTag>;

    struct RHIGraphicsPipelineTag;
    using GraphicsPipelineHandle = RHIHandle<RHIGraphicsPipelineTag>;

}