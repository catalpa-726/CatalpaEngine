#pragma once
#include "Materials/MaterialParameter.h"
#include "Materials/MaterialRenderState.h"

namespace Catalpa {
    struct ShaderAsset;
    // 作者shader字段只持逻辑身份；加载后的schema另保活确切Shader版本，不拥有管线或反射库对象。
    struct MaterialDefinition {
        uint32_t schemaVersion = 1;
        std::string family;
        MaterialShadingModel shadingModel = MaterialShadingModel::Unlit;
        MaterialDomain domain = MaterialDomain::Surface;
        MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
        AssetHandle<ShaderAsset> shader;
        // 仅运行期解析结果，不序列化此C++引用；旧定义不会随shader逻辑ID换版而追新。
        AssetVersionRef<ShaderAsset> shaderVersion;
        std::string vertexEntry = "vertexMain", fragmentEntry = "fragmentMain";
        std::string parameterBlock = "Material";
        std::vector<MaterialParameter> parameters;
        MaterialRenderState renderState;
        uint32_t allowedStateOverrides = 0;
    };
}
