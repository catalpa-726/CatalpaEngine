#pragma once
#include "catpch.h"

#include "Assets/AssetHandle.h"
#include "Shaders/ShaderMetadata.h"
#include "Shaders/ShaderVariantKey.h"
namespace Catalpa {
	/* 不可变CPU产物由AssetManager版本引用拥有；此处没有GPU句柄或编译器状态。 */
	struct ShaderAsset {
		std::vector<uint32_t> spirv;
		ShaderMetadata metadata;
		ShaderVariantKey variant;
	};
	/**
	 * @brief 校验着色器资产。
	 *
	 * @param[in] a 第一个着色器资产操作数。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateShaderAsset(const ShaderAsset&, std::string& error);
}
