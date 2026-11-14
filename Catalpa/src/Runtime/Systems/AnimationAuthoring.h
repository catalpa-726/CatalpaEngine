#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
#include "Assets/AssetId.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
	/**
	 * @brief 捕获动画组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in,out] fields 接收实体的动画与蒙皮组件记录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureAnimationComponents(
	    const World&, EntityId, std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 恢复动画组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 包含待恢复动画与蒙皮组件的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreAnimationComponents(
	    Scene&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 从场景快照恢复动画领域组件。
	 * 供已有 Scene 恢复事务调用；失败由外层子树事务回滚，不再排入下一批命令。
	 *
	 * @param[in,out] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 从场景归档读取的动画组件字段集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 声明的动画组件恢复成功时为 true，否则为 false，由外层事务处理回滚。
	 */
	CATALPA_API bool RestoreAnimationComponents(
	    World&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 校验动画组件。
	 *
	 * @param[in] key 指明要处理的动画组件种类的序列化键。
	 * @param[in] fields 待按该动画组件的格式验证的字段。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateAnimationComponent(std::string_view key, const ArchiveValue&, std::string& error);
	/**
	 * @brief 列出动画组件引用的资产标识。
	 *
	 * @param[in] key 指明要处理的动画组件种类的序列化键。
	 * @param[in] value 要读取或转换的归档值。
	 *
	 * @return 上述遍历或筛选得到的资产标识列表。
	 */
	CATALPA_API std::vector<AssetId> AnimationComponentDependencies(std::string_view key, const ArchiveValue&);
	/**
	 * @brief 绑定动画组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] dependencies 用于固定动画、骨架及蒙皮资源引用的依赖版本集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindAnimationComponentVersions(
	    World&, EntityId, std::span<const AssetDependencyValue>, std::string& error);
	/**
	 * @brief 收集动画组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] output 接收 std::vector<AssetDependencyValue> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CollectAnimationComponentVersions(
	    const World&, EntityId, std::vector<AssetDependencyValue>&, std::string& error);
} /* 命名空间 Catalpa */
