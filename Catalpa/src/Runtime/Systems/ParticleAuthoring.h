#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
	/**
	 * @brief 捕获粒子组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in,out] fields 接收实体的粒子发射器组件记录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureParticleComponents(
	    const World&, EntityId, std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 恢复粒子组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 包含待恢复粒子发射器记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreParticleComponents(
	    Scene&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 恢复粒子组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 包含待恢复粒子发射器记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreParticleComponents(
	    World&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 校验粒子组件。
	 *
	 * @param[in] type 组件文档中的类型键，此处必须为 catalpa.particle_emitter。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateParticleComponent(std::string_view, const ArchiveValue&, std::string& error);
	CATALPA_API std::vector<AssetId> ParticleComponentDependencies(std::string_view, const ArchiveValue&);
	/**
	 * @brief 绑定粒子组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] dependencies 用于固定粒子效果等资源引用的依赖版本集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindParticleComponentVersions(
	    World&, EntityId, std::span<const AssetDependencyValue>, std::string& error);
	/**
	 * @brief 收集粒子组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] output 接收 std::vector<AssetDependencyValue> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CollectParticleComponentVersions(
	    const World&, EntityId, std::vector<AssetDependencyValue>&, std::string& error);
} /* 命名空间 Catalpa */
