#pragma once
#include "Core/Base.h"
#include "Core/Serialization/Archive.h"
#include "ECS/World.h"
#include "Assets/AssetId.h"
#include "Scene/PersistentEntityId.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
	class Scene;
	/**
	 * @brief 捕获领域组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] output 接收 ArchiveValue::Object 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureDomainComponents(const Scene&, EntityId, ArchiveValue::Object&, std::string&);
	/**
	 * @brief 校验领域组件。
	 *
	 * @param[in] components 包含各领域组件及其序列化字段的映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateDomainComponents(const ArchiveValue::Object&, std::string&);
	CATALPA_API bool RestoreDomainComponents(Scene&, EntityId, const ArchiveValue::Object&, std::string&);
	CATALPA_API bool RestoreDomainComponents(World&, EntityId, const ArchiveValue::Object&, std::string&);
	/**
	 * @brief 列出领域组件引用的资产标识。
	 *
	 * @param[in] components 包含各领域组件及其序列化字段的映射。
	 *
	 * @return 上述遍历或筛选得到的资产标识列表。
	 */
	CATALPA_API std::vector<AssetId> DomainComponentDependencies(const ArchiveValue::Object&);
	/**
	 * @brief 收集领域组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] output 接收 std::vector<AssetDependencyValue> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CollectDomainComponentVersions(
	    const World&, EntityId, std::vector<AssetDependencyValue>&, std::string&);
	/**
	 * @brief 绑定领域组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] values 供该实体各领域组件绑定的固定资产版本集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindDomainComponentVersions(World&, EntityId, std::span<const AssetDependencyValue>, std::string&);
	/**
	 * @brief 绑定领域组件版本。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] values 供该实体各领域组件绑定的固定资产版本集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindDomainComponentVersions(Scene&, EntityId, std::vector<AssetDependencyValue>, std::string&);
	/**
	 * @brief 比较两者是否具有相同的领域组件。
	 *
	 * @param[in] left 左侧比较值。
	 * @param[in] right 右侧比较值。
	 *
	 * @return 满足上述条件时为 true，否则为 false。
	 */
	CATALPA_API bool SameDomainComponents(const ArchiveValue::Object&, const ArchiveValue::Object&);
	/**
	 * @brief 重新映射领域实体引用。
	 *
	 * @param[in,out] fields 包含待重映射实体引用的组件字段。
	 * @param[in] mapping 源与目标之间的映射关系。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RemapDomainEntityReferences(
	    ArchiveValue::Object&, const std::map<PersistentEntityId, PersistentEntityId>&, std::string& error);
} /* 命名空间 Catalpa */
