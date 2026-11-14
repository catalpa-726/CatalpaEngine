#pragma once
#include "Shaders/ShaderAsset.h"
namespace Catalpa {
	enum class RHIShaderStage { Vertex, Fragment, Compute };
	enum class RHIShaderVisibility { None, Vertex, Fragment, VertexFragment, Compute };
	/* 固定CPU资产版本拥有最终字节码及唯一元数据；入口字符串借用至同步创建结束。 */
	struct RHIShaderDesc {
		AssetVersionRef<ShaderAsset> asset;
		RHIShaderStage stage = RHIShaderStage::Vertex;
		const char* entryPoint = "main";
	};
	/**
	 * @brief 只检查容器/哈希/入口契约，运行时不执行编译器。
	 *
	 * @param[in] desc 要校验的固定版本着色器资产及其入口名称、着色阶段。
	 * @param[out] error 失败时写入资产无效、阶段不受支持或缺少对应入口的原因。
	 *
	 * @return 着色器资产有效且反射信息中含对应阶段和入口时返回 true。
	 */
	[[nodiscard]] CATALPA_API bool ValidateRHIShader(const RHIShaderDesc&, std::string& error);
}
