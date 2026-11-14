#include "catpch.h"

#include "Networking/LoopbackSettings.h"

#include "Core/Serialization/AuthorJson.h"
namespace Catalpa {
	namespace {
		bool Valid(const LoopbackSettings& value) {
			return value.tickRate >= 30 && value.tickRate <= 120 && value.limits.maxEntities > 0 &&
			    value.limits.maxEntities <= 256 && value.limits.maxTombstones > 0 &&
			    value.limits.maxTombstones <= 1024 && value.limits.maxRetiredIdentities >= value.limits.maxTombstones &&
			    value.limits.maxRetiredIdentities <= 65536 && value.limits.maxPacketBytes >= 1024 &&
			    value.limits.maxPacketBytes <= 60 * 1024 && value.faults.delayTicks <= 32 &&
			    value.faults.dropEvery <= 1000000 && value.faults.duplicateEvery <= 1000000;
		}
	} /* 命名空间 */
	bool ReadLoopbackSettings(std::string_view text, LoopbackSettings& output, std::string& error) try {
		const auto json = AuthorJson::Parse(text);
		AuthorJson::Fields(json,
		    {"schemaVersion", "transport", "tickRate", "maxEntities", "maxTombstones", "maxRetiredIdentities",
		        "maxPacketBytes", "delayTicks", "dropEvery", "duplicateEvery", "reorder"});
		if (AuthorJson::Unsigned(json.at("schemaVersion")) != 1 || json.at("transport") != "loopback")
			throw std::invalid_argument("Only loopback replication is enabled in this stage");
		const auto integer = [&](const char* key) {
			const auto value = AuthorJson::Unsigned(json.at(key));
			if (value > UINT32_MAX)
				throw std::invalid_argument("Loopback setting integer overflow");
			return uint32_t(value);
		};
		LoopbackSettings value;
		value.tickRate = integer("tickRate");
		value.limits.maxEntities = integer("maxEntities");
		value.limits.maxTombstones = integer("maxTombstones");
		value.limits.maxRetiredIdentities = integer("maxRetiredIdentities");
		value.limits.maxPacketBytes = integer("maxPacketBytes");
		value.faults.delayTicks = integer("delayTicks");
		value.faults.dropEvery = integer("dropEvery");
		value.faults.duplicateEvery = integer("duplicateEvery");
		value.faults.reverseDuePackets = json.at("reorder").get<bool>();
		if (!Valid(value)) {
			error = "Loopback configuration budgets are invalid";
			return false;
		}
		output = value;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool WriteLoopbackSettings(const LoopbackSettings& value, std::string& output, std::string& error) try {
		if (!Valid(value)) {
			error = "Loopback configuration budgets are invalid";
			return false;
		}
		output = AuthorJson::Json{{"schemaVersion", 1}, {"transport", "loopback"}, {"tickRate", value.tickRate},
		    {"maxEntities", value.limits.maxEntities}, {"maxTombstones", value.limits.maxTombstones},
		    {"maxRetiredIdentities", value.limits.maxRetiredIdentities},
		    {"maxPacketBytes", value.limits.maxPacketBytes}, {"delayTicks", value.faults.delayTicks},
		    {"dropEvery", value.faults.dropEvery}, {"duplicateEvery", value.faults.duplicateEvery},
		    {"reorder", value.faults.reverseDuePackets}}
		             .dump(2);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa */
