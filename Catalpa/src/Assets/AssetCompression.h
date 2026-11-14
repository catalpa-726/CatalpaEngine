#pragma once
#include "Assets/AssetContainerReader.h"
namespace Catalpa {
	/**
	 * @brief 将资产容器编码成带原始长度及摘要的压缩格式，压缩无收益时保留原格式。
	 * 格式 2 为格式 1 的 zstd 封装；明确记录解压长度和原容器 SHA-256。
	 *
	 * @param[in] value 待序列化的资产容器，包括标识、依赖和载荷。
	 * @param[out] out 成功时接收压缩容器；若压缩所需缓冲区超过 limit，或节省空间不足以抵消容器头，则接收原始序列化字节。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] maxBytes 未压缩容器及最终输出允许的最大字节数，同时限制临时压缩缓冲区。
	 *
	 * @return 生成预算内的完整容器时为 true，否则为 false。
	 */
	CATALPA_API bool WriteCompressedAssetContainer(
	    const AssetContainer&, std::vector<std::byte>&, std::string& error, size_t maxBytes = 64 * 1024 * 1024);
	/**
	 * @brief 读取压缩资产容器。
	 *
	 * @param[in] input 输入的字节序列。
	 * @param[out] out 接收 AssetContainer 结果。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] maxPayloadBytes 单个容器解压后允许的最大载荷字节数。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadCompressedAssetContainer(
	    std::span<const std::byte>, AssetContainer&, std::string& error, size_t maxPayloadBytes);
} /* 命名空间 Catalpa */
