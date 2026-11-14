#pragma once

#include "Core/Memory/GenerationalHandle.h"
#include "RHITypes.h"

/* 在通用槽位句柄外增加设备身份，并区分不同 GPU 资源。 */
namespace Catalpa {

	template <typename Tag> struct RHIHandle {
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
	/* 同一设备管线池负责寿命；绑定时严格验证 graphics/compute 种类。 */
	using ComputePipelineHandle = GraphicsPipelineHandle;

	struct RHIBufferTag;
	using BufferHandle = RHIHandle<RHIBufferTag>;

	struct RHISamplerTag;
	using SamplerHandle = RHIHandle<RHISamplerTag>;

}