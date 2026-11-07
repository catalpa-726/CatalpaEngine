#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 各导入器共用的中间顶点。字段显式排列，避免依赖第三方数学类型的内存布局。
    struct ImportVertex {
        std::array<float, 3> position{}, normal{};
        std::array<float, 2> uv{};
        std::array<float, 4> tangent{1, 0, 0, 1}, color{1, 1, 1, 1};
    };

    // 校验顶点和索引，再按需生成逐面法线和逐角 MikkTSpace 切线，发布独立网格资源。
    bool BuildImportedMesh(AssetBuildContext&, std::vector<ImportVertex>, std::vector<uint32_t>,
                           bool hasNormals, bool hasUV, bool hasTangents, MeshAsset&, std::string& error,
                           std::vector<uint32_t>* sourceVertexRemap = nullptr);
    // 将坐标轴和单位转换放在模型根节点，使各网格保留源顶点和实例关系。
    bool SetImportRoot(AssetBuildContext&, ModelAsset&, const Math::Mat4& sourceConversion,
                       std::string& error);
}
