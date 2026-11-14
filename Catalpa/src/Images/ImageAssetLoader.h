#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetCodec.h"
#include "Images/ImageAsset.h"
namespace Catalpa {
	class AssetManager;
	class CATALPA_API ImageAssetLoader {
	public:
		/**
		 * @brief 编码有效载荷。
		 *
		 * @param[in] asset 包含子资源布局和像素的图像资产。
		 * @param[out] output 接收编码后的图像资产载荷。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodePayload(const ImageAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 解码有效载荷。
		 *
		 * @param[in] bytes 包含图像元数据和像素的已编码载荷。
		 * @param[in] context 提供资产依赖及解码预算的上下文。
		 * @param[out] output 接收经验证的图像及其子资源布局。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodePayload(
		    std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
		/**
		 * @brief 创建图像资产在烘焙端及运行时共用的编解码器。
		 * 烘焙工具和运行时注册共用同一套带类型的载荷编解码协议。
		 *
		 * @return 带图像类型标识、解码回调和编码回调的编解码器。
		 */
		static AssetCodec Codec();
	};
	CATALPA_API bool RegisterImageAssetLoader(AssetManager&, std::string& error);
}
