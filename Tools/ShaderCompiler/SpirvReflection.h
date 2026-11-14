#pragma once
#include "catpch.h"

#include "Shaders/ShaderMetadata.h"
namespace Catalpa::Tools {
	/* 只处理已通过最终 spirv-val 验证的字节码。 */
	class SpirvReflection final {
	public:
		/**
		 * @brief 从 SPIR-V 模块提取入口、资源绑定与接口布局。
		 *
		 * @param[in] validatedWords 以 32 位字排列的 SPIR-V 模块。
		 * @param[out] output 接收 ShaderMetadata 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Reflect(std::span<const uint32_t> validatedWords, ShaderMetadata& output, std::string& error);
	};
}
