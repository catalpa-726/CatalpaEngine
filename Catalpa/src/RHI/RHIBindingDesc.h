#pragma once

#include "RHIHandle.h"
#include "RHIShaderDesc.h"
#include "RHITextureDesc.h"

namespace Catalpa {

	inline constexpr uint32_t RHIMaxBindingsPerSet = 32;

	enum class RHIBindingType { SampledTexture, Sampler, UniformBuffer, StorageBuffer };

	/* set 0单描述符；按反射限定graphics/compute阶段，buffer范围显式声明。 */
	struct RHIBindingLayoutEntry {
		uint32_t binding = 0;
		RHIBindingType type = RHIBindingType::SampledTexture;
		RHIShaderVisibility visibility = RHIShaderVisibility::Fragment;
		uint32_t minimumBufferBytes = 0; /* 最终ShaderMetadata中的UBO容量。 */
		RHITextureViewType imageViewType = RHITextureViewType::Texture2D;
	};

	/* 创建管线时同步复制 entries，调用结束后不再借用此 span。 */
	struct RHIBindingLayoutDesc {
		std::span<const RHIBindingLayoutEntry> entries{};
	};

	/* 只填写type对应句柄；UBO提供字节offset/size，其余类型这些字段保持0。 */
	struct RHIBinding {
		uint32_t binding = 0;
		RHIBindingType type = RHIBindingType::SampledTexture;
		TextureViewHandle texture{};
		SamplerHandle sampler{};
		BufferHandle buffer{};
		uint64_t offset = 0; /* UniformBuffer字节范围，不是动态offset。 */
		uint64_t size = 0;
		/* Compute写输出承诺覆盖整个范围；非完整写必须保持false并预先初始化。 */
		bool initializesRange = false;
	};

}
