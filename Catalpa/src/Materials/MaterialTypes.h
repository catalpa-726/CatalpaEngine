#pragma once
#include "catpch.h"

namespace Catalpa {
	/* 材质策略独立于 RHI 枚举，实际能否绘制还取决于着色器和后端校验。 */
	enum class MaterialShadingModel { Unlit, Pbr };
	enum class MaterialDomain { Surface };
	enum class MaterialAlphaMode { Opaque, Mask, Blend };
	enum class MaterialParameterType { Float, Float2, Float3, Float4, Int, UInt, Bool, Texture2D };
	enum class MaterialTextureSemantic { BaseColor, Normal, MetallicRoughness, Emissive, Occlusion, Data };
	enum class BuiltinMaterialKind { Unlit, Pbr, Error };
}
