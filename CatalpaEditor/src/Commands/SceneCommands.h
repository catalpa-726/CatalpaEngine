#pragma once
#include "Documents/SceneDocument.h"
#include "Core/Math/TransformMath.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/ScriptComponent.h"
#include "Scene/Components/BehaviorGraphComponent.h"

namespace Catalpa::Editor {
	/**
	 * @brief 创建带持久标识的作者实体，并登记对应的撤销、重做操作。
	 * UI 只提交意图；变更和历史记录在同一安全点完成。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] name 新实体在场景层级中显示的名称。
	 * @param[in] parent 父实体的持久标识，空标识表示放在场景根下。
	 * @param[out] created 成功时接收新实体的持久标识。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 创建事务执行成功并写回持久标识时为 true。
	 */
	bool CreateAuthorEntity(
	    SceneDocument&, std::string name, PersistentEntityId parent, PersistentEntityId& created, std::string& error);
	/**
	 * @brief 通过可撤销命令删除指定实体及其子树。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] id 用于定位目标的持久实体标识。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool DeleteAuthorEntity(SceneDocument&, PersistentEntityId, std::string& error);
	/**
	 * @brief 将剪贴板完整子树作为单次可撤销操作粘贴到原父节点下。
	 * @param[in,out] doc 目标作者场景文档。
	 * @param[in] snapshot 复制时的完整作者快照，必须属于目标场景。
	 * @param[out] created 成功时接收新子树根身份。
	 * @param[out] error 失败时接收原因，场景和历史均保持原样。
	 * @return 完整子树提交成功时为 true。
	 */
	bool PasteAuthorSubtree(
	    SceneDocument&, const SceneSubtreeSnapshot&, PersistentEntityId& created, std::string& error);
	/**
	 * @brief 为实体添加具有独立视图身份的相机，并记录可撤销操作。
	 * @param[in,out] doc 持有作者场景与命令历史的文档。
	 * @param[in] id 目标实体的持久标识。
	 * @param[out] error 已有相机、视图身份耗尽或命令失败时接收原因。
	 * @return 相机和历史同时提交成功时为 true。
	 */
	bool AddAuthorCamera(SceneDocument&, PersistentEntityId, std::string& error);
	/**
	 * @brief 设置完整网格和材质插槽，保留所引用的固定资产版本。
	 * @param[in,out] doc 持有作者场景与命令历史的文档。
	 * @param[in] id 目标实体的持久标识。
	 * @param[in] mesh 经过场景校验的候选网格和材质组件。
	 * @param[out] error 失败时接收原因，保留原组件和历史。
	 * @return 完整组件提交成功时为 true。
	 */
	bool SetAuthorMesh(SceneDocument&, PersistentEntityId, const MeshRendererComponent&, std::string& error);
	/**
	 * @brief 移除指定的可选渲染组件，撤销时恢复完整作者值和资产引用。
	 * @param[in,out] doc 持有作者场景与命令历史的文档。
	 * @param[in] id 目标实体的持久标识。
	 * @param[in] type 支持的相机、光源、环境或网格组件的 C++ 短名称。
	 * @param[out] error 不支持该类型或提交失败时接收原因。
	 * @return 可撤销的移除操作提交成功时为 true。
	 */
	bool RemoveAuthorComponent(SceneDocument&, PersistentEntityId, std::string_view type, std::string& error);
	/**
	 * @brief 修改局部。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] id 用于定位目标的持久实体标识。
	 * @param[in] before 操作前的状态。
	 * @param[in] after 操作后的状态。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ChangeLocal(SceneDocument&, PersistentEntityId, const Math::TransformTRS& before,
	    const Math::TransformTRS& after, std::string& error);
	/**
	 * @brief 检查原名并通过可撤销命令修改实体名称。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] id 用于定位目标的持久实体标识。
	 * @param[in] name 实体重命名后在层级面板显示的名称。
	 * @param[in] expected 用于比较的期望值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool RenameEntity(SceneDocument&, PersistentEntityId, std::string, const std::string& expected, std::string&);
	/**
	 * @brief 编辑相机。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] id 用于定位目标的持久实体标识。
	 * @param[in] next 下一状态的 CameraComponent。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool EditCamera(SceneDocument&, PersistentEntityId, const CameraComponent& next, std::string&);
	/**
	 * @brief 更新实体的父子关系。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] child 要重新挂接的子实体的持久标识。
	 * @param[in] parent 父节点或父实体。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ReparentEntity(SceneDocument&, PersistentEntityId child, PersistentEntityId parent, std::string& error);
	bool EditLight(SceneDocument&, PersistentEntityId, const LightComponent&, std::string&);
	bool EditEnvironment(SceneDocument&, PersistentEntityId, const EnvironmentComponent&, std::string&);
	/**
	 * @brief 编辑网格显示。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] id 用于定位目标的持久实体标识。
	 * @param[in] color 写入网格渲染组件的线性 RGBA 颜色。
	 * @param[in] visible 控制可见性的标志。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool EditMeshDisplay(SceneDocument&, PersistentEntityId, Math::Vec4 color, bool visible, std::string&);
	bool EditScript(SceneDocument&, PersistentEntityId, std::optional<ScriptComponent>, std::string&);
	bool EditBehaviorGraph(SceneDocument&, PersistentEntityId, std::optional<BehaviorGraphComponent>, std::string&);
}
