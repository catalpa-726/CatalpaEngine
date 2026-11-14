#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 导入器读取源文件，生成拥有独立存储的 CPU 领域数据并记录输入依赖。 */
	class IAssetImporter {
	public:
		virtual ~IAssetImporter() = default;
		/**
		 * @brief 返回用于识别或显示此项的名称。
		 *
		 * @return 导入器用于标识自身实现的非拥有名称视图，内容由具体导入器提供。
		 */
		virtual std::string_view Name() const noexcept = 0;
		/**
		 * @brief 读取源文件并生成后续资产烘焙所需的导入内容。
		 * @details 第 1 个参数（AssetBuildContext&）为输入。提供项目、源文件及预算的构建上下文。
		 * @details 第 2 个参数（AssetImportResult&）为输出。接收导入器生成的资产与依赖。
		 *
		 * @param[out] error 导入失败时的诊断信息。
		 *
		 * @return 源资产已转换为后续构建所需的导入结果时为 true；失败时通过错误参数说明原因。
		 */
		virtual bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const = 0;
	};
}
