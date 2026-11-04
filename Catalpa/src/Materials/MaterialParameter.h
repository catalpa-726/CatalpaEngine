#pragma once
#include "catpch.h"
#include "Assets/AssetHandle.h"
#include "Images/ImageAsset.h"
#include "Materials/MaterialTypes.h"

namespace Catalpa {
    // variant仅用于CPU受检值，磁盘及GPU布局按显式type/反射编码，不保存variant索引或sizeof。
    using MaterialValue = std::variant<float, std::array<float, 2>, std::array<float, 3>,
        std::array<float, 4>, int32_t, uint32_t, bool, AssetHandle<ImageAsset>>;
    struct MaterialParameter {
        std::string id;
        std::string displayName;
        MaterialParameterType type = MaterialParameterType::Float;
        MaterialValue defaultValue = 0.0f;
        std::optional<double> minimum, maximum;
        bool allowOverride = true;
        MaterialTextureSemantic textureSemantic = MaterialTextureSemantic::Data;
        bool allowTextureFallback = false;
    };
    // 稳定参数ID而非任意字节偏移；未声明ID及类型不匹配都必须拒绝。
    struct MaterialParameterOverride { std::string parameterId; MaterialValue value = 0.0f; };
}
