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
    // 只交换显式字段，不复制组件内存；完整 upsert 和带确认的 tombstone 承受 Loopback 故障注入。
    class CATALPA_API ReplicationService {
      public:
        ReplicationService(std::unique_ptr<NetworkSession>, ReplicationConfig,
                           ReplicationSchema = DefaultReplicationSchema());
        bool Publish(std::span<const ReplicatedEntityState>, uint64_t tick, std::string& error);
        bool Receive(std::string& error);
        const std::map<StableId, ReplicatedEntityState>& Entities() const noexcept {
            return m_State;
        }
        ReplicationStats Stats() const noexcept;
        const ReplicationConfig& Config() const noexcept {
            return m_Config;
        }
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
} // namespace Catalpa
