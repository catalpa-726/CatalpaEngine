#pragma once
#include "Audio/AudioClipAsset.h"
namespace Catalpa {
	class AssetManager;
	class CATALPA_API AudioAssetLoader final {
	public:
		/**
		 * @brief 验证音频片段并将其编码为运行时二进制负载。
		 *
		 * @param[in] value 待编码的音频片段。
		 * @param[out] out 接收音频资产负载。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Encode(const AudioClipAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 编码数据块。
		 *
		 * @param[in] samples 按左右声道交错排列的浮点 PCM 样本，两个样本构成一帧。
		 * @param[out] out 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodeChunk(std::span<const float>, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 解码数据块。
		 *
		 * @param[in] c 包含已编码音频块载荷的资产容器。
		 * @param[out] out 接收 AudioChunkAsset 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodeChunk(const AssetContainer&, AudioChunkAsset&, std::string& error);
		/**
		 * @brief 向资产管理器注册音频片段与流送分块的解码器。
		 *
		 * @param[in] manager 将负责加载音频资源的资产管理器。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Register(AssetManager&, std::string& error);
	};
} /* 命名空间 Catalpa */
