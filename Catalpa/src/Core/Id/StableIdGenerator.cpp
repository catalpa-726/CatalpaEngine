#include "catpch.h"

#include "Core/Id/StableIdGenerator.h"

#include "Core/PlatformServices.h"

namespace Catalpa {
	bool GenerateStableId(StableId& output, std::string& error) {
		StableId candidate;
		if (!FillRandomBytes(std::as_writable_bytes(std::span(candidate.bytes)), error))
			return false;
		/* UUIDv4 的版本位和变体位只约束新生成的标识，旧 StableId 保留原有含义。 */
		candidate.bytes[6] = static_cast<uint8_t>((candidate.bytes[6] & 0x0f) | 0x40);
		candidate.bytes[8] = static_cast<uint8_t>((candidate.bytes[8] & 0x3f) | 0x80);
		output = candidate;
		error.clear();
		return true;
	}
}
