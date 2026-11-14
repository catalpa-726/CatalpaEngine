#pragma once
#include "AssetPipeline/AssetBuildContext.h"
namespace Catalpa::Tools {
	/**
	 * @brief 按材质槽为基础网格生成较低细节层并更新模型引用。
	 * 不覆盖 LOD0 或来源文件；额外产物按材质槽分别简化，保留拓扑边界和 UV/法线接缝。
	 *
	 * @param[in,out] context 导入任务共享的项目、依赖与构建预算。
	 * @param[in,out] model 需要登记各网格细节层级的模型。
	 * @param[in,out] imported 持有源网格并接收新生成的简化网格资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 允许的细节层处理完毕时为 true，取消、预算不足或数据无效时为 false；无法简化的层可跳过。
	 */
	bool BuildModelLods(AssetBuildContext&, ModelAsset&, AssetImportResult&, std::string& error);
	/**
	 * @brief 生成给定简化比例的网格 LOD。
	 *
	 * @param[in] input 输入的 MeshAsset。
	 * @param[in] triangleRatio 比例系数。
	 * @param[in] relativeError 错误上限。
	 * @param[out] output 接收 MeshAsset 结果。
	 * @param[in] absoluteError 绝对错误。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool SimplifyMeshLod(const MeshAsset&, float triangleRatio, float relativeError, MeshAsset&, float& absoluteError,
	    std::string& error);
} /* 命名空间 Catalpa::Tools */
