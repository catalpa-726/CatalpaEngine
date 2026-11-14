#pragma once
#include "Scene/PrefabInstantiation.h"

namespace Catalpa {
	class CATALPA_API PrefabReconciler {
	public:
		/**
		 * @brief 描述覆盖值。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] root 遍历起点的持久实体标识。
		 * @param[out] output 接收 std::vector<std::string> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DescribeOverrides(
		    const Scene&, PersistentEntityId root, std::vector<std::string>&, std::string& error);
		/**
		 * @brief 比较现有预制体实例、来源及受支持的覆盖，准备更新或还原后的子树。
		 * 先准备并校验完整候选；支持名字、局部 TRS、实例颜色/可见性覆盖。
		 * 其他组件或结构冲突明确拒绝，不把无法表达的修改静默重置。
		 *
		 * @param[in] scene 包含现有预制体实例的场景。
		 * @param[in] root 待协调的实例根节点的持久标识。
		 * @param[in] version 用于准备候选实例的预制体版本。
		 * @param[in] asset 待应用的预制体内容。
		 * @param[in] assets 可选资产管理器，提供组件引用的资源。
		 * @param[in] resolver 按资产标识取得被引用预制体及其版本的回调。
		 * @param[in] revertOverrides 为 true 时撤销本地覆盖，按预制体原值准备候选子树。
		 * @param[out] candidate 成功时接收准备好的替换子树快照。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 可完整表达并准备候选子树时为 true；出现冲突或依赖校验失败时为 false。
		 */
		static bool Prepare(Scene&, PersistentEntityId root, uint64_t version, const PrefabAsset&, AssetManager*,
		    const PrefabResolver&, bool revertOverrides, SceneSubtreeSnapshot& candidate, std::string& error);
		/**
		 * @brief 用预先准备的候选子树替换实例，并保存替换前的快照。
		 * 用候选子树替换实例，保留事务所需的原始快照。
		 *
		 * @param[in,out] scene 实际执行子树替换的场景。
		 * @param[in] root 被替换子树根节点的持久标识。
		 * @param[in] candidate 拟写入场景的完整候选子树。
		 * @param[out] previous 成功时接收被替换的旧子树，供撤销使用。
		 * @param[in] expected 可选的旧子树快照，用于检查准备期间是否发生其他编辑。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 子树成功替换时为 true；校验、移除或恢复任一步失败时为 false。
		 */
		static bool Replace(Scene&, PersistentEntityId root, const SceneSubtreeSnapshot& candidate,
		    SceneSubtreeSnapshot& previous, const SceneSubtreeSnapshot* expected, std::string& error);
	};
}
