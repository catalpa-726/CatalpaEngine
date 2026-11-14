#pragma once
#include "Assets/PackageFormat.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
	class PackageBuilder final {
	public:
		/**
		 * @brief 追踪启动资产的精确版本依赖，并生成可独立读取的资产包。
		 * 挂载表由项目配置显式构造；闭包中的每个精确版本必须可读取。
		 *
		 * @param[in] source 提供待打包资产及其固定版本、依赖关系的资产目录。
		 * @param[in] files 供读取已挂载资产及依赖文件的虚拟文件系统。
		 * @param[in] entryScene 包入口资产标识，用于确定需要纳入包的依赖闭包。
		 * @param[in] absoluteOutput 生成 .catpkg 文件的绝对路径。
		 * @param[in] cancellation 用于查询取消请求的令牌。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] limits 打包时适用的条目和字节预算。
		 *
		 * @return 入口的依赖闭包完整且资产包成功写出时为 true，取消、版本不符或超限时为 false。
		 */
		static bool Build(const AssetRegistry&, const VirtualFileSystem&, AssetId entryScene,
		    const std::filesystem::path& absoluteOutput, CancellationToken, std::string& error,
		    PackageLimits limits = {});
	};
} /* 命名空间 Catalpa::Tools */
