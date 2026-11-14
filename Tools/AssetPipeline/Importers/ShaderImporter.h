#pragma once
#include "ShaderCompiler/ShaderCompileJob.h"
namespace Catalpa::Tools {
	/* S04 只处理着色器导入，多种资产类型的通用导入调度留给后续资产管线阶段。 */
	class ShaderImporter final {
	public:
		/**
		 * @brief 读取着色器并生成后续资产烘焙所需的导入内容。
		 *
		 * @param[in] request 描述本次请求的 ShaderCompileRequest。
		 * @param[out] output 输出的导入对象与依赖记录。
		 * @param[out] error 导入失败时的诊断信息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Import(const ShaderCompileRequest&, ShaderCompileResult&, std::string& error);
	};
}
