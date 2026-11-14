#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 同时生成作者材质定义、材质实例以及独立的 Unlit 预览，并固定各图像和着色器的内容版本。 */
	class MaterialCooker final {
	public:
		/**
		 * @brief 解析已准备的材质依赖并生成与目标着色器绑定的材质产物。
		 *
		 * @param[in] ctx 导入任务共享的项目、依赖与构建预算。
		 * @param[in] asset 提供源材质描述和纹理映射的已导入模型数据。
		 * @param[in] shader 为材质产物固定版本的着色器记录。
		 * @param[in] ready 按资产标识索引的已就绪构建记录，用于固定依赖版本。
		 * @param[out] output 接收 std::vector<AssetBuildProduct> 结果。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 材质定义、实例和独立预览的构建产物均已生成时为 true。
		 */
		static bool Cook(AssetBuildContext&, const ImportedAsset&, const AssetRecord& shader,
		    const std::map<AssetId, AssetRecord>& ready, std::vector<AssetBuildProduct>&, std::string& error);
	};
}
