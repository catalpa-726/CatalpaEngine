#pragma once
#include "catpch.h"

#include "Shaders/ShaderAsset.h"
#include "Assets/AssetId.h"
namespace Catalpa::Tools {
	/* 将经过验证和反射的不可变领域对象写入统一容器，命令的输出策略负责发布文件。 */
	class ShaderCooker final {
	public:
		/**
		 * @brief 将验证过的着色器内容编码为带身份与版本的资产容器。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] asset 本次操作使用的着色器资产。
		 * @param[out] output 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Cook(AssetId, const ShaderAsset&, std::vector<std::byte>& output, std::string& error);
	};
}
