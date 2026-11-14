#pragma once
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
namespace Catalpa {
	class AssetManager;
	/**
	 * @brief 注册物理资产加载器。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterPhysicsAssetLoaders(AssetManager&, std::string& error);
} /* 命名空间 Catalpa */
