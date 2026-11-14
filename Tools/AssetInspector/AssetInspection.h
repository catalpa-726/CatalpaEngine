#pragma once
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetRegistry.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
	struct AssetInspectionLimits {
		size_t maxAssetBytes = 640 * 1024 * 1024;
		size_t maxTotalBytes = size_t(2) * 1024 * 1024 * 1024;
		size_t maxResidentBytes = size_t(1) * 1024 * 1024 * 1024;
		size_t maxEntries = 65536;
		double timeoutSeconds = 120;
	};
	struct AssetInspection {
		AssetId id;
		std::string type, sha256;
		uint64_t version = 0;
		size_t payloadBytes = 0;
		std::vector<AssetDependency> dependencies;
		/* 成功表示领域 CPU loader 已真实解码；不等同于设备上传、脚本执行或 UI 布局验收。 */
		bool domainValidated = false;
		/**
		 * @brief 检查无需外部依赖的单个资产容器。
		 * 独立无依赖容器可直接检查；带依赖容器须提供目录，避免把缺少闭包误报为成功。
		 *
		 * @param[in] file 待读取的独立构建容器的绝对路径。
		 * @param[out] output 成功时接收这个容器的检查结果。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] maxBytes 单个容器允许读取和解码的最大字节数。
		 *
		 * @return 容器成功读取、校验并生成独立检查结果时为 true；依赖外部资产的容器须改用闭包检查。
		 */
		static bool Read(
		    const std::filesystem::path&, AssetInspection&, std::string& error, size_t maxBytes = 640 * 1024 * 1024);
		/**
		 * @brief 验证资产目录及其传递依赖，并汇集闭包检查报告。
		 * roots 为空检查整张目录，否则只检查这些根的传递闭包，包括惰性音频块。
		 * 使用生产注册器和无设备 AssetManager，失败保留调用者先前的报告。
		 *
		 * @param[in] catalog 提供资产身份、确切版本与定位信息的目录。
		 * @param[in] files 供读取已挂载资产及依赖文件的虚拟文件系统。
		 * @param[in] roots 闭包遍历的起始资产标识；为空时检查整个目录。
		 * @param[out] output 成功时接收闭包内各资产的检查结果。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] cancellation 用于查询取消请求的令牌。
		 * @param[in] limits 限制单项及累积读取量等检查资源的上限。
		 *
		 * @return 所选范围内的资产与版本依赖均完成检查时为 true，取消或检查失败时为 false。
		 */
		static bool ReadClosure(const AssetRegistry&, const VirtualFileSystem&, std::span<const AssetId> roots,
		    std::vector<AssetInspection>&, std::string& error, CancellationToken = {}, AssetInspectionLimits = {});
	};
} /* 命名空间 Catalpa::Tools */
