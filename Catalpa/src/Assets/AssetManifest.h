#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetRegistry.h"
namespace Catalpa {
	/* 运行时使用的小端序清单，只记录烘焙产物的位置及其完整容器摘要，不含源文件根目录。 */
	class CATALPA_API AssetManifest {
	public:
		/**
		 * @brief 校验二进制资产清单并据此重建注册表。
		 *
		 * @param[in] bytes 完整的清单字节。
		 * @param[out] output 接收校验通过的资产注册表。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Read(std::span<const std::byte>, AssetRegistry&, std::string& error);
		/**
		 * @brief 将完整资产注册表编码为二进制清单。
		 *
		 * @param[in] registry 已构建的资产注册表。
		 * @param[out] output 接收序列化后的清单字节。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Write(const AssetRegistry&, std::vector<std::byte>&, std::string& error);
	};
}
