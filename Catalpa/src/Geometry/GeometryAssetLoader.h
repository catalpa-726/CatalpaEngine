#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetLoader.h"
#include "Geometry/MeshAsset.h"
#include "Geometry/ModelAsset.h"
namespace Catalpa {
	class AssetManager;
	class CATALPA_API GeometryAssetLoader {
	public:
		/**
		 * @brief 编码网格。
		 *
		 * @param[in] asset 待保存的几何网格资产。
		 * @param[out] output 接收编码后的网格载荷。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodeMesh(const MeshAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 编码模型。
		 *
		 * @param[in] model 待保存的模型节点和网格实例。
		 * @param[out] output 接收编码后的模型载荷。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodeModel(const ModelAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 解码网格。
		 *
		 * @param[in] input 待解码的网格载荷字节。
		 * @param[in] context 解码所需的资产依赖和预算。
		 * @param[out] output 接收完整验证后的网格资产。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodeMesh(
		    std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
		/**
		 * @brief 解码模型。
		 *
		 * @param[in] input 待解码的模型载荷字节。
		 * @param[in] context 提供所需依赖与解码预算的上下文。
		 * @param[out] output 接收模型及其节点和实例。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodeModel(
		    std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
	};
	/**
	 * @brief 注册几何资产加载器。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterGeometryAssetLoaders(AssetManager&, std::string& error);
}
