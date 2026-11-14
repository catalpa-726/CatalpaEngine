#pragma once
#include "Networking/ReplicationSchema.h"
#include "Networking/NetworkSession.h"
namespace Catalpa {
	struct ReplicationConfig {
		uint32_t localPeer = 1, remotePeer = 2, serverPeer = 1;
		ReplicationLimits limits;
	};
	struct ReplicationStats {
		uint64_t sent = 0, received = 0, stale = 0, rejected = 0, spawned = 0, despawned = 0;
		size_t live = 0, pendingDespawn = 0;
		uint64_t lastTick = 0;
	};
	/* 只传输明确声明的字段。新增或更新时传递完整记录，删除时使用带确认的删除标记，以处理环回传输中的模拟故障。 */
	class CATALPA_API ReplicationService {
	public:
		/**
		 * @brief 初始化 ReplicationService。
		 *
		 * @param[in] session 接管所有权的网络会话，负责发送和接收复制消息。
		 * @param[in] config 控制快照发送和接收行为的复制服务配置。
		 * @param[in] schema 用于编码和验证快照的复制模式。
		 */
		ReplicationService(
		    std::unique_ptr<NetworkSession>, ReplicationConfig, ReplicationSchema = DefaultReplicationSchema());
		/**
		 * @brief 为新的模拟步编码并发送实体状态快照。
		 *
		 * @param[in] input 本模拟步待编码的实体状态序列。
		 * @param[in] tick 当前模拟时刻或步编号。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Publish(std::span<const ReplicatedEntityState>, uint64_t tick, std::string& error);
		/**
		 * @brief 推进传输时钟并处理已到达的复制消息。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Receive(std::string& error);
		const std::map<StableId, ReplicatedEntityState>& Entities() const noexcept { return m_State; }
		/**
		 * @brief 汇总当前复制状态、实体数与会话统计。
		 *
		 * @return 当前会话的复制统计快照。
		 */
		ReplicationStats Stats() const noexcept;
		const ReplicationConfig& Config() const noexcept { return m_Config; }
		/**
		 * @brief 关闭复制会话并清空已保存的状态。
		 */
		void Close() noexcept;

	private:
		bool Apply(const ReplicationSnapshot&, std::string& error);
		std::unique_ptr<NetworkSession> m_Session;
		ReplicationConfig m_Config;
		ReplicationSchema m_Schema;
		ReplicationStats m_Stats;
		std::map<StableId, ReplicatedEntityState> m_State, m_Published;
		std::map<StableId, ReplicationDespawn> m_Tombstones;
		std::map<StableId, uint64_t> m_Retired;
		uint64_t m_PublishedTick = 0, m_ReceivedTick = 0, m_TransportTick = 0;
	};
} /* 命名空间 Catalpa */
