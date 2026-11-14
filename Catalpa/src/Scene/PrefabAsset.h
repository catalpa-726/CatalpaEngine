#pragma once
#include "Scene/SceneAsset.h"

namespace Catalpa {
	struct PrefabNest {
		StableId id;
		AssetId asset;
		PersistentEntityId parent;
		Math::TransformTRS local;
	};
	struct PrefabAsset {
		SceneAsset scene;
		std::vector<PrefabNest> nested;
		/* 加载器绑定烘焙产物声明的依赖版本，展开嵌套引用时沿用该版本。 */
		std::map<AssetId, AssetVersionRef<PrefabAsset>> nestedVersions;
	};
	/**
	 * @brief 读取预制体。
	 *
	 * @param[in] text 包含预制体实体层级与嵌套引用的 JSON 文本。
	 * @param[out] output 接收 PrefabAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadPrefab(std::string_view text, PrefabAsset&, std::string& error);
	/**
	 * @brief 写入预制体。
	 *
	 * @param[in] asset 待序列化的预制体定义。
	 * @param[out] output 成功时接收包含预制体层级和嵌套引用的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WritePrefab(const PrefabAsset&, std::string&, std::string& error);
	/**
	 * @brief 为预制体资产注册资产加载器。
	 * @details 第 1 个参数（class AssetManager&）为输入。提供资产查找和版本访问的管理器。
	 *
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 操作成功时为 true，失败时为 false。
	 */
	CATALPA_API bool RegisterPrefabAssetLoader(class AssetManager&, std::string& error);
}
