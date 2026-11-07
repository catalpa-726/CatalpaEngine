#include "catpch.h"
#include "Networking/ReplicationService.h"

namespace Catalpa {
    ReplicationService::ReplicationService(std::unique_ptr<NetworkSession> session, ReplicationConfig config,
                                           ReplicationSchema schema)
        : m_Session(std::move(session)), m_Config(config), m_Schema(std::move(schema)) {
        std::string error;
        if (!m_Session || !config.localPeer || !config.remotePeer || !config.serverPeer ||
            config.localPeer == config.remotePeer ||
            (config.serverPeer != config.localPeer && config.serverPeer != config.remotePeer) ||
            !ValidateReplicationSchema(m_Schema, error))
            throw std::invalid_argument(
                error.empty() ? "Replication requires two distinct peers and one explicit server" : error);
    }
    ReplicationStats ReplicationService::Stats() const noexcept {
        auto stats = m_Stats;
        stats.live = m_State.size();
        stats.pendingDespawn = m_Tombstones.size();
        stats.lastTick = m_ReceivedTick;
        if (m_Session)
            stats.stale += m_Session->DroppedDuplicates();
        return stats;
    }
    void ReplicationService::Close() noexcept {
        if (m_Session)
            m_Session->Close();
        m_State.clear();
        m_Published.clear();
        m_Tombstones.clear();
        m_Retired.clear();
    }

    bool ReplicationService::Publish(std::span<const ReplicatedEntityState> input, uint64_t tick,
                                     std::string& error) try {
        if (!m_Session || !tick || tick <= m_PublishedTick || tick < m_TransportTick ||
            input.size() > m_Config.limits.maxEntities) {
            error = "Replication publish tick/order or entity budget rejected";
            return false;
        }
        std::map<StableId, ReplicatedEntityState> published;
        auto tombstones = m_Tombstones;
        auto retired = m_Retired;
        const bool server = m_Config.localPeer == m_Config.serverPeer;
        for (auto entity : input) {
            entity.tick = tick;
            entity.spawn = server;
            if (!ValidateReplicatedEntity(entity, error) || !published.emplace(entity.id, entity).second) {
                if (error.empty())
                    error = "Duplicate published network identity";
                return false;
            }
            if (!server && (entity.owner != m_Config.localPeer || !m_State.contains(entity.id) ||
                            m_State.at(entity.id).generation != entity.generation)) {
                error = "A client may publish only an already spawned entity that it owns";
                return false;
            }
            if (server && retired.contains(entity.id) && entity.generation <= retired.at(entity.id)) {
                error = "Respawn requires a strictly newer network generation";
                return false;
            }
        }
        if (server)
            for (const auto& [id, previous] : m_Published)
                if (!published.contains(id) || published.at(id).generation != previous.generation) {
                    const ReplicationDespawn despawn{id, previous.generation, tick};
                    tombstones[id] = despawn;
                    retired[id] = std::max(retired[id], previous.generation);
                }
        if (tombstones.size() > m_Config.limits.maxTombstones ||
            retired.size() > m_Config.limits.maxRetiredIdentities) {
            error = "Replication tombstone/retired identity budget exhausted; wait for acknowledgements or "
                    "reconnect";
            return false;
        }
        ReplicationSnapshot snapshot;
        snapshot.schema = m_Schema.id;
        snapshot.schemaVersion = m_Schema.version;
        snapshot.sender = m_Config.localPeer;
        snapshot.tick = tick;
        snapshot.acknowledged = m_ReceivedTick;
        for (const auto& [id, entity] : published)
            snapshot.entities.push_back(entity);
        if (server)
            for (const auto& [id, despawn] : tombstones)
                snapshot.despawns.push_back(despawn);
        std::vector<std::byte> bytes;
        if (!EncodeReplicationSnapshot(snapshot, bytes, error, m_Config.limits))
            return false;
        // 传输时钟即使遇到发送背压也已前进；失败重试不能让 Receive 把它倒退。
        m_TransportTick = tick;
        if (!m_Session->AdvanceTick(tick, error))
            return false;
        if (!m_Session->Send(1, bytes, error))
            return false;
        m_Published.swap(published);
        m_Tombstones.swap(tombstones);
        m_Retired.swap(retired);
        m_PublishedTick = tick;
        ++m_Stats.sent;
        if (server) {
            for (const auto& [id, despawn] : m_Tombstones)
                if (auto found = m_State.find(id);
                    found != m_State.end() && found->second.generation <= despawn.generation)
                    m_State.erase(found);
            for (const auto& [id, entity] : m_Published)
                m_State[id] = entity;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool ReplicationService::Apply(const ReplicationSnapshot& snapshot, std::string& error) try {
        if (snapshot.sender != m_Config.remotePeer || snapshot.schema != m_Schema.id ||
            snapshot.schemaVersion != m_Schema.version || snapshot.acknowledged > m_PublishedTick) {
            error = "Replication peer/schema or acknowledgement authority mismatch";
            return false;
        }
        if (snapshot.tick <= m_ReceivedTick) {
            ++m_Stats.stale;
            error.clear();
            return true;
        }
        const bool authority = snapshot.sender == m_Config.serverPeer;
        if (!authority && !snapshot.despawns.empty()) {
            error = "Only the server may despawn replicated entities";
            return false;
        }
        auto candidate = m_State;
        auto retired = m_Retired;
        uint64_t spawned = 0, despawned = 0;
        for (const auto& deleted : snapshot.despawns) {
            retired[deleted.id] = std::max(retired[deleted.id], deleted.generation);
            if (auto existing = candidate.find(deleted.id);
                existing != candidate.end() && existing->second.generation <= deleted.generation) {
                candidate.erase(existing);
                ++despawned;
            }
        }
        const auto permits = [&](uint32_t field) {
            const auto found = std::find_if(m_Schema.fields.begin(), m_Schema.fields.end(),
                                            [&](const auto& description) { return description.id == field; });
            return authority || found->authority == ReplicationAuthority::Owner;
        };
        for (const auto& entity : snapshot.entities) {
            if (retired.contains(entity.id) && entity.generation <= retired.at(entity.id)) {
                ++m_Stats.stale;
                continue;
            }
            auto existing = candidate.find(entity.id);
            if (!authority) {
                if (entity.spawn || existing == candidate.end() ||
                    existing->second.owner != snapshot.sender || entity.owner != snapshot.sender ||
                    existing->second.generation != entity.generation ||
                    existing->second.persistent != entity.persistent) {
                    error = "Client update violates spawn, generation or ownership authority";
                    return false;
                }
                const auto& old = existing->second;
                if ((!permits(1) && old.local.translation != entity.local.translation) ||
                    (!permits(2) && old.local.rotation != entity.local.rotation) ||
                    (!permits(3) && old.local.scale != entity.local.scale) ||
                    (!permits(4) && old.name != entity.name) ||
                    (!permits(5) && old.visible != entity.visible)) {
                    error = "Client update changes a server-authoritative schema field";
                    return false;
                }
            }
            if (existing != candidate.end()) {
                if (entity.generation < existing->second.generation) {
                    ++m_Stats.stale;
                    continue;
                }
                if (entity.generation == existing->second.generation &&
                    entity.persistent != existing->second.persistent) {
                    error = "A live network generation cannot change persistent entity identity";
                    return false;
                }
                if (entity.generation > existing->second.generation) {
                    if (!entity.spawn) {
                        error = "New network generation requires an explicit spawn";
                        return false;
                    }
                    retired[entity.id] = std::max(retired[entity.id], existing->second.generation);
                    ++spawned;
                }
            } else {
                if (!entity.spawn) {
                    error = "Unknown network entity requires an explicit spawn";
                    return false;
                }
                ++spawned;
            }
            candidate[entity.id] = entity;
        }
        if (candidate.size() > m_Config.limits.maxEntities ||
            retired.size() > m_Config.limits.maxRetiredIdentities) {
            error = "Replication receiver state exceeds entity/history bounds";
            return false;
        }
        std::set<PersistentEntityId> identities;
        for (const auto& [id, entity] : candidate)
            if (!identities.insert(entity.persistent).second) {
                error = "Network identities conflict on one persistent entity";
                return false;
            }
        m_State.swap(candidate);
        m_Retired.swap(retired);
        m_ReceivedTick = snapshot.tick;
        m_Stats.spawned += spawned;
        m_Stats.despawned += despawned;
        std::erase_if(m_Tombstones,
                      [&](const auto& entry) { return entry.second.tick <= snapshot.acknowledged; });
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool ReplicationService::Receive(std::string& error) try {
        if (!m_Session || !m_Session->AdvanceTick(m_TransportTick, error))
            return false;
        for (size_t i = 0; i < 256; ++i) {
            uint32_t type = 0;
            NetworkMessage message;
            bool available = false;
            if (!m_Session->Receive(type, message, available, error))
                return false;
            if (!available) {
                error.clear();
                return true;
            }
            ReplicationSnapshot snapshot;
            if (type != 1 || !DecodeReplicationSnapshot(message.payload, snapshot, error, m_Config.limits) ||
                !Apply(snapshot, error)) {
                ++m_Stats.rejected;
                return false;
            }
            ++m_Stats.received;
        }
        error = "Replication receive packet budget exceeded";
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
} // namespace Catalpa
