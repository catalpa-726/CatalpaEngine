#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
	/* 将 glTF/GLB 静态场景导入为模型、网格和材质。外部缓冲和贴图由只读源根解析。 */
	class GltfImporter final : public IAssetImporter {
	public:
		std::string_view Name() const noexcept override { return "fastgltf/core-v4-cani1-cskn2-lod-mips-bc1"; }

		/**
		 * @brief 读取 glTF 模型并生成后续资产烘焙所需的导入内容。
		 *
		 * @param[in] ctx 导入任务共享的项目、依赖与构建预算。
		 * @param[out] output 输出的导入对象与依赖记录。
		 * @param[out] error 导入失败时的诊断信息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
	};
}
