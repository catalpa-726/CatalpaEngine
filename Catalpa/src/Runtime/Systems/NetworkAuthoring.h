#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
namespace Catalpa {
	/**
	 * @brief 捕获网络组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in,out] fields 接收实体的网络复制组件记录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureNetworkComponents(
	    const World&, EntityId, std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 恢复网络组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 包含待恢复网络组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreNetworkComponents(
	    Scene&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 恢复网络组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] fields 包含待恢复网络组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreNetworkComponents(
	    World&, EntityId, const std::map<std::string, ArchiveValue, std::less<>>&, std::string& error);
	/**
	 * @brief 校验网络组件。
	 *
	 * @param[in] type 组件文档中的类型键，此处必须为 catalpa.network_identity。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateNetworkComponent(std::string_view, const ArchiveValue&, std::string& error);
} /* 命名空间 Catalpa */
