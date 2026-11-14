#pragma once
#include "Core/Base.h"
#include "Assets/AssetLoadResult.h"
#include "ECS/World.h"
namespace Catalpa {
	/**
	 * @brief 绑定物理导航地形版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] values 用于解析物理、导航和地形组件引用的固定资产版本集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindPhysicsNavigationTerrainVersions(
	    World&, EntityId, std::span<const AssetDependencyValue>, std::string&);
	/**
	 * @brief 收集物理导航地形版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in,out] values 接收物理、导航和地形组件实际持有的资产版本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CollectPhysicsNavigationTerrainVersions(
	    const World&, EntityId, std::vector<AssetDependencyValue>&, std::string&);
} /* 命名空间 Catalpa */
