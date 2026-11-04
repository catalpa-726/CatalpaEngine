#include "catpch.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/PlatformServices.h"

namespace Catalpa {
    bool GenerateStableId(StableId& output, std::string& error)
    {
        StableId candidate;
        if (!FillRandomBytes(std::as_writable_bytes(std::span(candidate.bytes)), error)) return false;
        // UUIDv4/version与variant位仅约束新生成值，不重新解释旧StableId。
        candidate.bytes[6] = static_cast<uint8_t>((candidate.bytes[6] & 0x0f) | 0x40);
        candidate.bytes[8] = static_cast<uint8_t>((candidate.bytes[8] & 0x3f) | 0x80);
        output = candidate;
        error.clear();
        return true;
    }
}
