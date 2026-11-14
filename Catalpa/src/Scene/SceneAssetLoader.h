#pragma once
#include "Assets/AssetLoader.h"
#include "Scene/SceneAsset.h"

namespace Catalpa {
	class AssetManager;
	/**
	 * @brief 为场景资产注册资产加载器。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterSceneAssetLoader(AssetManager&, std::string& error);
}
