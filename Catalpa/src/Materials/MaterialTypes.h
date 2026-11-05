#pragma once
#include "catpch.h"

namespace Catalpa {
    // 领域策略独立于具体RHI枚举；具体可绘制能力仍由实际shader和后端校验决定。
    enum class MaterialShadingModel { Unlit, Pbr };
    enum class MaterialDomain { Surface };
    enum class MaterialAlphaMode { Opaque, Mask, Blend };
    enum class MaterialParameterType { Float, Float2, Float3, Float4, Int, UInt, Bool, Texture2D };
    enum class MaterialTextureSemantic { BaseColor, Normal, MetallicRoughness, Emissive, Occlusion, Data };
    enum class BuiltinMaterialKind { Unlit, Pbr, Error };
}
