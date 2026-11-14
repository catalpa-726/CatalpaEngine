#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
	/* 从 FBX 或 OBJ 读取模型，保留材质和外部文件引用，在模型根节点应用坐标转换。 */
	class AssimpImporter final : public IAssetImporter {
	public:
		std::string_view Name() const noexcept override { return "assimp/fbx-obj-v5-cani1-cskn2-lod-mips-bc1"; }

		/**
		 * @brief 读取经 Assimp 解析的模型并生成后续资产烘焙所需的导入内容。
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
