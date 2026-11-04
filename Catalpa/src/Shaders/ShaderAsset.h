#pragma once
#include "catpch.h"
#include "Assets/AssetHandle.h"
#include "Shaders/ShaderMetadata.h"
#include "Shaders/ShaderVariantKey.h"
namespace Catalpa {
    // 不可变CPU产物由AssetManager版本引用拥有；此处没有GPU句柄或编译器状态。
    struct ShaderAsset {
        std::vector<uint32_t> spirv;
        ShaderMetadata metadata;
        ShaderVariantKey variant;
    };
    CATALPA_API bool ValidateShaderAsset(const ShaderAsset&, std::string& error);
}
