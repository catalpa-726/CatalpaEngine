#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
	/* 领域解析和引用收集在工具层完成；运行时只读取已 Cook 的固定版本容器。 */
	class AnimationImporter final : public IAssetImporter {
	public:
		explicit AnimationImporter(std::string type)
		    : m_Type(std::move(type)), m_Name("author-animation-" + m_Type + "-v3-cani1-cskn2") {}
		std::string_view Name() const noexcept override { return m_Name; }
		/**
		 * @brief 读取动画并生成后续资产烘焙所需的导入内容。
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
} /* 命名空间 Catalpa::Tools */
