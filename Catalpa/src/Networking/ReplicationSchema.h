#pragma once
#include "Core/Base.h"
#include "Core/Math/TransformMath.h"
#include "Scene/PersistentEntityId.h"

namespace Catalpa {
	enum class ReplicationAuthority { Server, Owner };
	enum class ReplicationFieldType { Vector3, Quaternion, String, Boolean };
	struct ReplicationField {
		uint32_t id = 0;
		std::string name;
		ReplicationFieldType type = ReplicationFieldType::Vector3;
		ReplicationAuthority authority = ReplicationAuthority::Server;
	};
	struct ReplicationSchema {
		StableId id;
		uint32_t version = 1;
		std::vector<ReplicationField> fields;
	};
	struct ReplicatedEntityState {
		StableId id;
		PersistentEntityId persistent;
		uint64_t generation = 1, tick = 0;
		uint32_t owner = 1;
		bool spawn = true, visible = true;
		std::string name;
		Math::TransformTRS local;
	};
	struct ReplicationDespawn {
		StableId id;
		uint64_t generation = 1, tick = 0;
	};
	struct ReplicationSnapshot {
		StableId schema;
		uint32_t schemaVersion = 1, sender = 0;
		uint64_t tick = 0, acknowledged = 0;
		std::vector<ReplicatedEntityState> entities;
		std::vector<ReplicationDespawn> despawns;
	};
	struct ReplicationLimits {
		size_t maxEntities = 64, maxTombstones = 256, maxRetiredIdentities = 1024, maxPacketBytes = 60 * 1024;
	};
	/**
	 * @brief 构建默认的状态复制模式。
	 *
	 * @return 默认的复制协议描述，供发送端和接收端使用相同的字段约定。
	 */
	CATALPA_API ReplicationSchema DefaultReplicationSchema();
	/**
	 * @brief 校验状态复制模式。
	 *
	 * @param[in] schema 指定复制字段和协议约束的模式定义。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateReplicationSchema(const ReplicationSchema&, std::string& error);
	/**
	 * @brief 校验参与复制的实体。
	 *
	 * @param[in] value 将按复制协议检查的单个实体状态。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateReplicatedEntity(const ReplicatedEntityState&, std::string& error);
	/**
	 * @brief 编码状态复制快照。
	 *
	 * @param[in] snapshot 待发送的复制快照。
	 * @param[out] output 接收序列化后的快照字节。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 编码时允许的消息及条目上限。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool EncodeReplicationSnapshot(
	    const ReplicationSnapshot&, std::vector<std::byte>&, std::string& error, ReplicationLimits = {});
	/**
	 * @brief 解码状态复制快照。
	 *
	 * @param[in] bytes 待验证的复制快照数据包。
	 * @param[out] output 接收经完整性和预算检查的快照。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 接收方允许的消息大小与条目数量上限。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool DecodeReplicationSnapshot(
	    std::span<const std::byte>, ReplicationSnapshot&, std::string& error, ReplicationLimits = {});
} /* 命名空间 Catalpa */
