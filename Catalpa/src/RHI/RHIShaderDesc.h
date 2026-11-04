#pragma once
#include "Shaders/ShaderAsset.h"
namespace Catalpa {
    enum class RHIShaderStage { Vertex, Fragment };
    enum class RHIShaderVisibility { None, Vertex, Fragment, VertexFragment };
    // 固定CPU资产版本拥有最终字节码及唯一元数据；入口字符串借用至同步创建结束。
    struct RHIShaderDesc {
        AssetVersionRef<ShaderAsset> asset;
        RHIShaderStage stage = RHIShaderStage::Vertex;
        const char* entryPoint = "main";
    };
    // 只检查容器/哈希/入口契约，运行时不执行编译器。
    [[nodiscard]] CATALPA_API bool ValidateRHIShader(const RHIShaderDesc&, std::string& error);
}
