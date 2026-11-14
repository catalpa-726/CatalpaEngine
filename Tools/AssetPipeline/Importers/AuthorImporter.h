#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
	/* 作者领域仍分别解析/验证；这里只统一只读输入、身份与容器输出的工具接线。 */
	class AuthorImporter final : public IAssetImporter {
	public:
		explicit AuthorImporter(std::string type) : m_Type(std::move(type)), m_Name("author-" + m_Type + "-v1") {}
		std::string_view Name() const noexcept override { return m_Name; }
		/**
		 * @brief 读取作者文档并生成后续资产烘焙所需的导入内容。
		 *
		 * @param[in] context 本次导入的源文件、依赖解析器及内存预算。
		 * @param[out] output 输出的导入对象与依赖记录。
		 * @param[out] error 导入失败时的诊断信息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;

	private:
		std::string m_Type, m_Name;
	};
}
