#pragma once
#include "ShaderCompileJob.h"
namespace Catalpa::Tools {
	class SlangCompiler final {
	public:
		/**
		 * @brief 调用 Slang 编译器并收集 SPIR-V、源文件依赖与编译诊断。
		 * 调用 Slang 编译器生成 SPIR-V 并记录入口反射和依赖文件。
		 *
		 * @param[in] job 提供源模块、包含目录、编译目标和取消令牌的请求。
		 * @param[in] temporaryRoot 本作业存放 SPIR-V、依赖文件、反射信息和日志的临时目录。
		 * @param[out] words 成功时接收 Slang 编译得到的 SPIR-V 字序列。
		 * @param[out] dependencies 接收编译器依赖文件中记录的输入路径。
		 * @param[out] diagnostics 接收 Slang 编译进程的诊断文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return Slang 编译成功且字节码与依赖信息成功读回时为 true。
		 */
		static bool Compile(const ShaderCompileRequest&, const std::filesystem::path& temporaryRoot,
		    std::vector<uint32_t>& words, std::vector<std::string>& dependencies, std::string& diagnostics,
		    std::string& error);
		/**
		 * @brief 用 Slang 的反射报告核对最终模块的入口名称与阶段。
		 * Slang辅助反射只核对入口/stage；binding/offset事实始终来自最终SPIR-V。
		 *
		 * @param[in] json Slang 输出的入口反射 JSON 文本。
		 * @param[in] metadata 从最终 SPIR-V 得到的元数据，用于交叉核对入口名与阶段。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return Slang 报告的入口名称和阶段与模块元数据匹配时为 true。
		 */
		static bool CheckEntryReflection(std::string_view json, const ShaderMetadata&, std::string& error);
	};
}
