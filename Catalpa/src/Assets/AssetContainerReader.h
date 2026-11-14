#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetContainerFormat.h"
namespace Catalpa {
	class CATALPA_API AssetContainerReader {
	public:
		/**
		 * @brief 读取资产容器并限制解码负载尺寸，支持带压缩标记的版本。
		 *
		 * @param[in] input 完整的容器字节。
		 * @param[out] output 成功时写入容器头、依赖和负载。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] maxPayloadBytes 解码后负载允许占用的最大字节数。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Read(
		    std::span<const std::byte>, AssetContainer&, std::string& error, size_t maxPayloadBytes = 64 * 1024 * 1024);
	};
	/**
	 * @brief 将资产头、依赖版本和载荷序列化到内存。
	 * 工具与测试共用内存编码器；由调用者决定是否将结果写入文件。
	 *
	 * @param[in] data 待写入容器的资产头、依赖版本表及载荷。
	 * @param[out] output 成功时写入完整容器的字节序列。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] maxBytes 此输出缓冲区允许写入的最大字节数。
	 *
	 * @return 容器合法且在字节预算内写完时为 true，否则为 false。
	 */
	CATALPA_API bool WriteAssetContainer(
	    const AssetContainer&, std::vector<std::byte>& output, std::string& error, size_t maxBytes = 64 * 1024 * 1024);
	/**
	 * @brief 计算资产内容哈希。
	 *
	 * @param[in] input 输入的字节序列。
	 * @param[out] output 成功时接收由 64 个小写十六进制字符组成的 SHA-256 摘要。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ComputeAssetContentHash(std::span<const std::byte>, std::string& output, std::string& error);
}
