#pragma once
#include "catpch.h"

#include "Assets/AssetHandle.h"
#include "Images/ImageAsset.h"
#include "Materials/MaterialTypes.h"

namespace Catalpa {
	/* 变体仅表示 CPU 端已校验的参数值。磁盘与 GPU 数据按显式类型和反射信息编码，不写入变体索引或 C++ 对象大小。 */
	using MaterialValue = std::variant<float, std::array<float, 2>, std::array<float, 3>, std::array<float, 4>, int32_t,
	    uint32_t, bool, AssetHandle<ImageAsset>>;
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
	/* 稳定参数ID而非任意字节偏移；未声明ID及类型不匹配都必须拒绝。 */
	struct MaterialParameterOverride {
		std::string parameterId;
		MaterialValue value = 0.0f;
	};
}
