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
    CATALPA_API ReplicationSchema DefaultReplicationSchema();
    CATALPA_API bool ValidateReplicationSchema(const ReplicationSchema&, std::string& error);
    CATALPA_API bool ValidateReplicatedEntity(const ReplicatedEntityState&, std::string& error);
    CATALPA_API bool EncodeReplicationSnapshot(const ReplicationSnapshot&, std::vector<std::byte>&,
                                               std::string& error, ReplicationLimits = {});
    CATALPA_API bool DecodeReplicationSnapshot(std::span<const std::byte>, ReplicationSnapshot&,
                                               std::string& error, ReplicationLimits = {});
} // namespace Catalpa
