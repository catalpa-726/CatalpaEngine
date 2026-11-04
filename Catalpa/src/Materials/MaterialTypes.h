#pragma once
#include "catpch.h"

namespace Catalpa {
    // 领域策略独立于具体RHI枚举；声明PBR数据不等于当前Renderer已实现PBR着色。
    enum class MaterialShadingModel { Unlit, Pbr };
    enum class MaterialDomain { Surface };
    enum class MaterialAlphaMode { Opaque, Mask, Blend };
    enum class MaterialParameterType { Float, Float2, Float3, Float4, Int, UInt, Bool, Texture2D };
    enum class MaterialTextureSemantic { BaseColor, Normal, MetallicRoughness, Emissive, Occlusion, Data };
    enum class BuiltinMaterialKind { Unlit, Pbr, Error };
}
