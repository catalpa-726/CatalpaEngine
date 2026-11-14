#pragma once
#include "AssetPipeline/IAssetImporter.h"
namespace Catalpa::Tools {
	class FontImporter final : public IAssetImporter {
	public:
		std::string_view Name() const noexcept override { return "FreeType-font-v1"; }
		/**
		 * @brief 读取字体并生成后续资产烘焙所需的导入内容。
		 *
		 * @param[in] context 本次导入的源文件、依赖解析器及内存预算。
		 * @param[out] out 接收 AssetImportResult 结果。
		 * @param[out] error 导入失败时的诊断信息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;
	};
} /* 命名空间 Catalpa::Tools */
