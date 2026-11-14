#pragma once
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa::Tools {
	class BehaviorGraphCompiler {
	public:
		/**
		 * @brief 解析并验证行为图，生成具有运行预算约束的解释程序。
		 * 输出验证过的解释程序；控制流允许环，由 Runtime 的指令/队列预算约束。
		 *
		 * @param[in] source 记录行为图节点与连接的作者文档文本。
		 * @param[out] output 成功时接收通过节点和控制流校验的行为图资产。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 行为图成功编译为已验证的解释程序时为 true。
		 */
		static bool Compile(std::string_view source, BehaviorGraphAsset&, std::string& error);
	};
}
