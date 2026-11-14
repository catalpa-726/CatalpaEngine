#pragma once
#include "Materials/MaterialParameter.h"
#include "Materials/MaterialRenderState.h"

namespace Catalpa {
	struct ShaderAsset;
	/* 作者数据中的着色器字段只保存资产标识。加载后的材质定义保活所用着色器的确切版本，管线和反射对象由其他模块持有。 */
	struct MaterialDefinition {
		uint32_t schemaVersion = 1;
		std::string family;
		MaterialShadingModel shadingModel = MaterialShadingModel::Unlit;
		MaterialDomain domain = MaterialDomain::Surface;
		MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
		AssetHandle<ShaderAsset> shader;
		/* 此引用只属于运行时解析结果，不参与序列化。着色器发布新版本后，旧材质定义仍固定原有版本。 */
		AssetVersionRef<ShaderAsset> shaderVersion;
		std::string vertexEntry = "vertexMain", fragmentEntry = "fragmentMain";
		std::string parameterBlock = "Material";
		std::vector<MaterialParameter> parameters;
		MaterialRenderState renderState;
		uint32_t allowedStateOverrides = 0;
	};
}
