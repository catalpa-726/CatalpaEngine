#include "catpch.h"

#include "Networking/ReplicationSchema.h"

#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 校验复制协议文本是否使用合法编码。
		 *
		 * @param[in] value 待检查 UTF-8 编码和控制字符的复制文本。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		bool Text(std::string_view value) {
			for (size_t i = 0; i < value.size();) {
				const uint8_t lead = uint8_t(value[i++]);
				if (lead < 0x80) {
					if (lead < 0x20 || lead == 0x7f)
						return false;
					continue;
				}
				uint32_t code = 0, minimum = 0;
				size_t count = 0;
				if ((lead & 0xe0) == 0xc0) {
					code = lead & 0x1f;
					count = 1;
					minimum = 0x80;
				} else if ((lead & 0xf0) == 0xe0) {
					code = lead & 15;
					count = 2;
					minimum = 0x800;
				} else if ((lead & 0xf8) == 0xf0) {
					code = lead & 7;
					count = 3;
					minimum = 0x10000;
				} else
					return false;
				if (count > value.size() - i)
					return false;
				for (size_t k = 0; k < count; ++k) {
					const uint8_t byte = uint8_t(value[i++]);
					if ((byte & 0xc0) != 0x80)
						return false;
					code = (code << 6) | (byte & 0x3f);
				}
				if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
					return false;
			}
			return true;
		}

		bool Limits(const ReplicationLimits& value) {
			return value.maxEntities > 0 && value.maxEntities <= 256 && value.maxTombstones > 0 &&
			    value.maxTombstones <= 1024 && value.maxRetiredIdentities >= value.maxTombstones &&
			    value.maxRetiredIdentities <= 65536 && value.maxPacketBytes >= 1024 &&
			    value.maxPacketBytes <= 60 * 1024;
		}

		bool WriteId(BinaryWriter& writer, StableId id) {
			return writer.Bytes(std::as_bytes(std::span(id.bytes)));
		}

		bool ReadId(BinaryReader& reader, StableId& id) {
			std::span<const std::byte> bytes;
			if (!reader.Bytes(16, bytes))
				return false;
			std::memcpy(id.bytes.data(), bytes.data(), 16);
			return id.HasValue();
		}

		bool WriteTransform(BinaryWriter& writer, const Math::TransformTRS& value) {
			return writer.F32(value.translation.x) && writer.F32(value.translation.y) &&
			    writer.F32(value.translation.z) && writer.F32(value.rotation.w) && writer.F32(value.rotation.x) &&
			    writer.F32(value.rotation.y) && writer.F32(value.rotation.z) && writer.F32(value.scale.x) &&
			    writer.F32(value.scale.y) && writer.F32(value.scale.z);
		}

		bool ReadTransform(BinaryReader& reader, Math::TransformTRS& value) {
			return reader.F32(value.translation.x) && reader.F32(value.translation.y) &&
			    reader.F32(value.translation.z) && reader.F32(value.rotation.w) && reader.F32(value.rotation.x) &&
			    reader.F32(value.rotation.y) && reader.F32(value.rotation.z) && reader.F32(value.scale.x) &&
			    reader.F32(value.scale.y) && reader.F32(value.scale.z);
		}

		bool Snapshot(const ReplicationSnapshot& value, const ReplicationLimits& limits, std::string& error) {
			if (!Limits(limits) || !value.schema.HasValue() || value.schemaVersion != 1 || !value.sender ||
			    !value.tick || value.entities.size() > limits.maxEntities ||
			    value.despawns.size() > limits.maxTombstones) {
				error = "Replication snapshot header/schema/budget is invalid";
				return false;
			}
			std::map<StableId, uint64_t> ids;
			std::set<PersistentEntityId> persistent;
			for (const auto& entity : value.entities) {
				if (!ValidateReplicatedEntity(entity, error) || entity.tick != value.tick ||
				    !ids.emplace(entity.id, entity.generation).second || !persistent.insert(entity.persistent).second) {
					if (error.empty())
						error = "Duplicate replication identity or inconsistent tick";
					return false;
				}
			}
			std::set<StableId> deleted;
			for (const auto& entity : value.despawns)
				if (!entity.id.HasValue() || !entity.generation || !entity.tick || entity.tick > value.tick ||
				    !deleted.insert(entity.id).second ||
				    (ids.contains(entity.id) && ids.at(entity.id) <= entity.generation)) {
					error = "Replication despawn identity/generation/tick is invalid";
					return false;
				}
			error.clear();
			return true;
		}
	} /* 命名空间 */
	ReplicationSchema DefaultReplicationSchema() {
		ReplicationSchema schema;
		schema.id = StableId::FromWords(0x4341545245504c31ULL, 1);
		schema.fields = {{1, "translation", ReplicationFieldType::Vector3, ReplicationAuthority::Owner},
		    {2, "rotation", ReplicationFieldType::Quaternion, ReplicationAuthority::Owner},
		    {3, "scale", ReplicationFieldType::Vector3, ReplicationAuthority::Owner},
		    {4, "name", ReplicationFieldType::String, ReplicationAuthority::Server},
		    {5, "visible", ReplicationFieldType::Boolean, ReplicationAuthority::Owner}};
		return schema;
	}

	bool ValidateReplicationSchema(const ReplicationSchema& schema, std::string& error) {
		if (!schema.id.HasValue() || schema.version != 1 || schema.fields.size() != 5) {
			error = "Replication v1 requires an explicit five-field schema";
			return false;
		}
		const auto expected = DefaultReplicationSchema();
		std::set<uint32_t> ids;
		for (const auto& field : schema.fields) {
			if (field.id < 1 || field.id > 5 || !ids.insert(field.id).second ||
			    field.name != expected.fields[field.id - 1].name || field.type != expected.fields[field.id - 1].type ||
			    (field.authority != ReplicationAuthority::Owner && field.authority != ReplicationAuthority::Server)) {
				error = "Replication schema has an unknown, duplicate or mismatched field";
				return false;
			}
		}
		error.clear();
		return true;
	}

	bool ValidateReplicatedEntity(const ReplicatedEntityState& value, std::string& error) {
		if (!value.id.HasValue() || !value.persistent.HasValue() || !value.generation || !value.owner ||
		    value.name.empty() || value.name.size() > 128 || !Text(value.name) ||
		    !Math::ValidateTRS(value.local).IsSuccess()) {
			error = "Replicated entity identity, generation, text or TRS is invalid";
			return false;
		}
		for (int i = 0; i < 3; ++i)
			if (std::abs(value.local.translation[i]) > 1000000 || std::abs(value.local.scale[i]) > 10000) {
				error = "Replicated transform exceeds the declared metre/scale range";
				return false;
			}
		error.clear();
		return true;
	}

	bool EncodeReplicationSnapshot(const ReplicationSnapshot& snapshot, std::vector<std::byte>& output,
	    std::string& error, ReplicationLimits limits) try {
		if (!Snapshot(snapshot, limits, error))
			return false;
		BinaryWriter writer(limits.maxPacketBytes);
		if (!writer.U32(0x52504c31) || !WriteId(writer, snapshot.schema) || !writer.U32(snapshot.schemaVersion) ||
		    !writer.U32(snapshot.sender) || !writer.U64(snapshot.tick) || !writer.U64(snapshot.acknowledged) ||
		    !writer.U32(uint32_t(snapshot.entities.size())) || !writer.U32(uint32_t(snapshot.despawns.size()))) {
			error = writer.Error();
			return false;
		}
		for (const auto& entity : snapshot.entities)
			if (!WriteId(writer, entity.id) || !WriteId(writer, entity.persistent.value) ||
			    !writer.U64(entity.generation) || !writer.U64(entity.tick) || !writer.U32(entity.owner) ||
			    !writer.U8(entity.spawn ? 1 : 0) || !writer.U8(entity.visible ? 1 : 0) || !writer.String(entity.name) ||
			    !WriteTransform(writer, entity.local)) {
				error = writer.Error();
				return false;
			}
		for (const auto& entity : snapshot.despawns)
			if (!WriteId(writer, entity.id) || !writer.U64(entity.generation) || !writer.U64(entity.tick)) {
				error = writer.Error();
				return false;
			}
		output = writer.Data();
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool DecodeReplicationSnapshot(std::span<const std::byte> bytes, ReplicationSnapshot& output, std::string& error,
	    ReplicationLimits limits) try {
		if (!Limits(limits) || bytes.size() > limits.maxPacketBytes) {
			error = "Replication packet byte budget exceeded";
			return false;
		}
		BinaryReader reader(bytes);
		ReplicationSnapshot value;
		uint32_t magic = 0, entities = 0, despawns = 0;
		if (!reader.U32(magic) || magic != 0x52504c31 || !ReadId(reader, value.schema) ||
		    !reader.U32(value.schemaVersion) || !reader.U32(value.sender) || !reader.U64(value.tick) ||
		    !reader.U64(value.acknowledged) || !reader.U32(entities) || !reader.U32(despawns) ||
		    entities > limits.maxEntities || despawns > limits.maxTombstones) {
			error = "Malformed replication packet header/count";
			return false;
		}
		value.entities.reserve(entities);
		value.despawns.reserve(despawns);
		for (uint32_t i = 0; i < entities; ++i) {
			ReplicatedEntityState entity;
			uint8_t spawn = 0, visible = 0;
			if (!ReadId(reader, entity.id) || !ReadId(reader, entity.persistent.value) ||
			    !reader.U64(entity.generation) || !reader.U64(entity.tick) || !reader.U32(entity.owner) ||
			    !reader.U8(spawn) || spawn > 1 || !reader.U8(visible) || visible > 1 ||
			    !reader.String(entity.name, 128) || !ReadTransform(reader, entity.local)) {
				error = "Malformed replicated entity fields";
				return false;
			}
			entity.spawn = spawn == 1;
			entity.visible = visible == 1;
			value.entities.push_back(std::move(entity));
		}
		for (uint32_t i = 0; i < despawns; ++i) {
			ReplicationDespawn entity;
			if (!ReadId(reader, entity.id) || !reader.U64(entity.generation) || !reader.U64(entity.tick)) {
				error = "Malformed replicated despawn";
				return false;
			}
			value.despawns.push_back(entity);
		}
		if (!reader.AtEnd() || !Snapshot(value, limits, error)) {
			if (error.empty())
				error = "Trailing replication bytes";
			return false;
		}
		output = std::move(value);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa */
