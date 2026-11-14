#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 各导入器共用的中间顶点。字段显式排列，避免依赖第三方数学类型的内存布局。 */
	struct ImportVertex {
		std::array<float, 3> position{}, normal{};
		std::array<float, 2> uv{};
		std::array<float, 4> tangent{1, 0, 0, 1}, color{1, 1, 1, 1};
	};

	/**
	 * @brief 校验顶点和索引，再按需生成逐面法线和逐角 MikkTSpace 切线，发布独立网格资源。
	 *
	 * @param[in,out] ctx 导入任务共享的项目、依赖与构建预算。
	 * @param[in] vertices 源网格各顶点的属性，按需展开或补齐缺失的属性。
	 * @param[in] indices 每三个索引指定一个源三角形。
	 * @param[in] hasNormals 源顶点是否已经具有法线。
	 * @param[in] hasUV 源顶点是否已经具有纹理坐标。
	 * @param[in] hasTangents 源顶点是否已经具有切线。
	 * @param[out] out 成功时接收包含顶点流、索引和材质槽的网格资产。
	 * @param[out] error 失败时接收当前操作的具体原因。
	 * @param[out] sourceVertexRemap 可选输出，按输出顶点顺序记录其对应的源顶点索引。
	 *
	 * @return 顶点、索引、顶点属性与输出网格均通过构建校验时为 true。
	 */
	bool BuildImportedMesh(AssetBuildContext&, std::vector<ImportVertex>, std::vector<uint32_t>, bool hasNormals,
	    bool hasUV, bool hasTangents, MeshAsset&, std::string& error,
	    std::vector<uint32_t>* sourceVertexRemap = nullptr);
	/**
	 * @brief 在模型根添加轴向和单位转换并校验最终层级。
	 * 将坐标轴和单位转换放在模型根节点，使各网格保留源顶点和实例关系。
	 *
	 * @param[in] ctx 导入任务共享的项目、依赖与构建预算。
	 * @param[in,out] model 接收新增转换根节点，并将原根节点重挂到该节点下的模型。
	 * @param[in] sourceConversion 源格式给出的导入根变换，在用户指定的轴与单位调整之前应用。
	 * @param[out] error 失败时接收当前操作的具体原因。
	 *
	 * @return 添加坐标转换根节点后模型通过校验时为 true。
	 */
	bool SetImportRoot(AssetBuildContext&, ModelAsset&, const Math::Mat4& sourceConversion, std::string& error);
}
