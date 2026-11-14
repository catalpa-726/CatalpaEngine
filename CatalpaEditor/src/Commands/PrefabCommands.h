#pragma once
#include "Documents/SceneDocument.h"
#include "Assets/AssetManager.h"

namespace Catalpa::Editor {
	/**
	 * @brief 实例化创作预制体。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] assets 提供资产查找和版本访问的管理器。
	 * @param[in] id 用于定位目标的资产标识。
	 * @param[in] parent 父节点或父实体。
	 * @param[out] created 成功时接收预制体实例根实体的持久标识。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool InstantiateAuthorPrefab(SceneDocument&, AssetManager&, AssetId, PersistentEntityId parent,
	    PersistentEntityId& created, std::string& error);
	/**
	 * @brief 更新创作预制体。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] assets 提供资产查找和版本访问的管理器。
	 * @param[in] root 遍历起点的持久实体标识。
	 * @param[in] revert 撤销修改。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool UpdateAuthorPrefab(SceneDocument&, AssetManager&, PersistentEntityId root, bool revert, std::string& error);
}
