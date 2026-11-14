#pragma once
#include "Documents/SceneDocument.h"
#include "Scene/PrefabAsset.h"

namespace Catalpa::Editor {
	/* Prefab 文件复用 AssetDocument 的源文档/历史；此适配器只产生可复用的作者子树。 */
	struct PrefabDocument {
		/**
		 * @brief 根据当前选择生成对应的数据。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] root 遍历起点的持久实体标识。
		 * @param[out] output 接收 PrefabAsset 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool FromSelection(SceneDocument&, PersistentEntityId root, PrefabAsset&, std::string& error);
	};
}
