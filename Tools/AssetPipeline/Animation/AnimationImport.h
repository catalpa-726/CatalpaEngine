#pragma once
#include "AssetPipeline/AssetBuildContext.h"
struct aiScene;
namespace fastgltf {
	struct Asset;
}

namespace Catalpa::Tools {
	using PrimitiveRemaps = std::map<std::pair<size_t, size_t>, std::vector<uint32_t>>;
	/**
	 * @brief 追加 Assimp 动画。
	 *
	 * @param[in] context 导入任务共享的项目、依赖与构建预算。
	 * @param[in] source 输入的 aiScene。
	 * @param[in,out] model 已导入的模型层级与网格实例；导入后写入关联骨架和动画资源引用。
	 * @param[out] result 接收 AssetImportResult 结果。
	 * @param[in] remaps 重映射表。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool AppendAssimpAnimations(AssetBuildContext&, const aiScene&, ModelAsset&, AssetImportResult&,
	    const std::vector<std::vector<uint32_t>>&, std::string& error);
	/**
	 * @brief 追加 glTF 动画。
	 *
	 * @param[in] context 导入任务共享的项目、依赖与构建预算。
	 * @param[in] asset 本次操作使用的 fastgltf::Asset。
	 * @param[in,out] model 已导入的模型层级与网格实例；导入后写入关联骨架和动画资源引用。
	 * @param[out] result 接收 AssetImportResult 结果。
	 * @param[in] remaps 重映射表。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool AppendGltfAnimations(AssetBuildContext&, const fastgltf::Asset&, ModelAsset&, AssetImportResult&,
	    const PrimitiveRemaps&, std::string& error);
} /* 命名空间 Catalpa::Tools */
