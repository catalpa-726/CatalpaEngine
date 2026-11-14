#pragma once
#include "catpch.h"

#include "Core/Jobs/CancellationToken.h"
#include "Shaders/ShaderAsset.h"
namespace Catalpa::Tools {
	struct ShaderCompileRequest {
		std::filesystem::path sourceRoot, sourceFile, sdkRoot;
		std::map<std::string, std::string, std::less<>> defines;
		CancellationToken cancellation;
	};
	struct ShaderCompileResult {
		std::shared_ptr<const ShaderAsset> asset;
		std::vector<std::string> dependencies;
		std::string diagnostics;
	};
	/* 离线任务依次固定源文件和工具、编译、派生 Invariant 装饰、验证并反射一次，所有步骤均受预算限制。 */
	class ShaderCompileJob final {
	public:
		/**
		 * @brief 执行着色器编译、验证和反射，并汇集依赖与诊断。
		 *
		 * @param[in] request 指定源码、包含根目录和编译选项的任务。
		 * @param[out] output 成功时接收编译字节码、入口元数据及依赖等构建结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 编译、验证和反射均成功并生成资产结果时为 true，取消或任一步骤失败时为 false。
		 */
		static bool Run(const ShaderCompileRequest&, ShaderCompileResult& output, std::string& error);
		/**
		 * @brief 计算用于比较内容或配置的指纹。
		 *
		 * @param[in] request 描述本次请求的 ShaderCompileRequest。
		 * @param[out] output 接收由 SDK、着色器入口文件路径、源文件快照及宏定义共同计算的内容指纹。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Fingerprint(const ShaderCompileRequest&, std::string& output, std::string& error);
	};
	namespace ShaderToolDetail {
		std::string PathText(const std::filesystem::path&);
		/**
		 * @brief 读取给定路径的文件内容。
		 *
		 * @param[in] p 参与处理的文件系统路径。
		 * @param[in] limit 允许读入的文件大小上限，单位为字节。
		 * @param[out] output 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ReadFile(const std::filesystem::path&, size_t, std::vector<std::byte>&, std::string&);
		/**
		 * @brief 启动指定子进程并收集执行结果。
		 *
		 * @param[in] exe 可执行文件。
		 * @param[in] arguments 调用时传入的参数集合。
		 * @param[in] diagnosticsFile 接收子进程诊断输出的日志路径。
		 * @param[in] cancel 用于检查取消请求的令牌。
		 * @param[in] diagnostics 诊断信息。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool RunProcess(const std::filesystem::path& exe, const std::vector<std::wstring>& arguments,
		    const std::filesystem::path& diagnosticsFile, CancellationToken, std::string& diagnostics,
		    std::string& error);
		/**
		 * @brief 为 SPIR-V 中的位置输出补充必要的 Invariant 装饰。
		 * 插入的装饰只是派生候选，完整合法性必须随后由 spirv-val 判定。
		 *
		 * @param[in] words 待补充位置不变装饰的 SPIR-V 模块，以 32 位字为单位。
		 * @param[out] output 接收添加装饰后的候选模块，随后还须通过 SPIR-V 校验。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 模块扫描及所需装饰的处理成功时为 true，模块结构无效时为 false；最终模块还须单独验证。
		 */
		bool AddPositionInvariant(std::span<const uint32_t>, std::vector<uint32_t>&, std::string&);
	}
}
