#pragma once

#include "Lighting/EnvironmentAsset.h"
#include "Assets/AssetCodec.h"

namespace Catalpa {

	class AssetManager;
	class CATALPA_API EnvironmentAssetLoader {
	public:
		/**
		 * @brief 编码有效载荷。
		 *
		 * @param[in] asset 待保存的环境光照资产。
		 * @param[out] output 接收编码后的环境载荷。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodePayload(const EnvironmentAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 解码有效载荷。
		 *
		 * @param[in] bytes 待还原为环境光照资源的载荷。
		 * @param[in] context 环境资源解码所用的依赖和预算。
		 * @param[out] output 接收解码后的环境资产。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodePayload(
		    std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
		/**
		 * @brief 构造环境资产的编解码器与依赖加载回调。
		 *
		 * @return 注册环境资产时使用的编解码器，包含该资产的类型约束与回调。
		 */
		static AssetCodec Codec();
	};
	CATALPA_API bool RegisterEnvironmentAssetLoader(AssetManager&, std::string& error);

}
