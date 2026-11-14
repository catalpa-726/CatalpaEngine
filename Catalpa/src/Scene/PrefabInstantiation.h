#pragma once
#include "Scene/PrefabAsset.h"
#include "Scene/SceneSubtree.h"

namespace Catalpa {
	class Scene;
	class AssetManager;
	/* resolver 返回固定来源版本，递归过程中不能切换到不同版本。 */
	using PrefabResolver = std::function<bool(AssetId, std::shared_ptr<const PrefabAsset>&, uint64_t&, std::string&)>;
	class CATALPA_API PrefabInstantiation {
	public:
		/**
		 * @brief 发布准备好的预制体子树并返回实例根实体。
		 *
		 * @param[in,out] scene 经过准备和校验后，将实例子树挂入的目标场景。
		 * @param[in] source 输入的资产标识。
		 * @param[in] version 该预制体来源资产的固定版本号。
		 * @param[in] asset 要展开为场景实体层级的预制体资产。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] resolve 按资产标识取得被引用预制体及其版本的回调。
		 * @param[in] parent 父节点或父实体。
		 * @param[out] root 成功时接收新实例根实体的持久标识。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Instantiate(Scene&, AssetId, uint64_t, const PrefabAsset&, AssetManager*, const PrefabResolver&,
		    PersistentEntityId parent, PersistentEntityId& root, std::string& error);
		/**
		 * @brief 展开预制体引用，并准备含固定依赖与持久身份的候选子树。
		 * 更新沿用旧映射中的身份；调用方负责用文档命令捕获被替换子树。
		 *
		 * @param[in] scene 接收预制体实例的目标场景。
		 * @param[in] source 用于追踪实例来源的预制体资产标识。
		 * @param[in] version 与当前预制体内容对应的非零版本。
		 * @param[in] asset 待展开的预制体内容。
		 * @param[in] assets 用于解析并固定展开子树所引用的资产版本。子树包含需要解析的组件资源时须提供。
		 * @param[in] resolve 按资产标识取得被引用预制体及其版本的回调。
		 * @param[in] parent 拟挂接的父实体持久标识，可为空。
		 * @param[in] reuse 从预制体源节点到已有实例节点的持久标识映射。
		 * @param[out] output 接收准备好的实例子树快照。
		 * @param[out] root 成功时接收实例根节点的持久标识。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 展开与依赖检查成功并交付候选快照和根标识时为 true，否则为 false。
		 */
		static bool Prepare(Scene&, AssetId, uint64_t, const PrefabAsset&, AssetManager*, const PrefabResolver&,
		    PersistentEntityId parent, const std::map<PersistentEntityId, PersistentEntityId>& reuse,
		    SceneSubtreeSnapshot&, PersistentEntityId& root, std::string& error);
	};
}
