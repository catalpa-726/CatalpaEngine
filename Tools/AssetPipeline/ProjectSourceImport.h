#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
	/** @brief 提交前展示的源文件闭包及不可变哈希；提交时核对同一清单。 */
	struct ProjectImportReview {
		std::filesystem::path destination;
		std::map<std::filesystem::path, std::string> inputs;
		std::vector<std::string> warnings;
	};
	/**
	 * @brief 扫描源文件依赖并生成导入确认清单。
	 * @param[in] request 包含源文件和项目解析配置的值副本。
	 * @param[in] directory 项目 Content 内的目标目录。
	 * @param[out] review 输入路径、内容哈希和将要生成的源文件路径。
	 * @param[out] error 缺少必要依赖或目标冲突时的具体原因。
	 * @return 已收集并校验依赖时为 true。
	 */
	bool ReviewProjectSource(AssetBuildRequest request, const std::filesystem::path& directory,
	    ProjectImportReview& review, std::string& error);
	/**
	 * @brief 收集导入器实际读取的源依赖，在项目内无覆盖地发布一份完整素材副本。
	 * @param[in,out] request 项目和原源文件，成功后 source 指向项目可构建的路径。
	 * @param[in] directory 项目 Content 内接收素材的目录，已挂载素材直接引用。
	 * @param[out] error 缺依赖、同名冲突、取消或复制失败的原因。
	 * @param[in] approved 可选的确认清单，输入变化后拒绝发布。
	 * @return 源文件可在该项目构建时为 true。
	 */
	bool ImportProjectSource(AssetBuildRequest& request, const std::filesystem::path& directory, std::string& error,
	    const ProjectImportReview* approved = nullptr);
}
