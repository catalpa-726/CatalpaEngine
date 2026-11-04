#include "catpch.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
    bool BinaryWriter::Bytes(std::span<const std::byte> bytes) {
        if (!m_Error.empty()) return false;
        if (m_Data.size() > m_Limit || bytes.size() > m_Limit - m_Data.size()) { m_Error = "Binary output exceeds its byte limit"; return false; }
        try { m_Data.insert(m_Data.end(), bytes.begin(), bytes.end()); return true; }
        catch (const std::exception& exception) { m_Error = exception.what(); return false; }
    }
    bool BinaryWriter::U8(uint8_t value) { const std::byte byte{value}; return Bytes({&byte, 1}); }
    bool BinaryWriter::U32(uint32_t value) { std::array<std::byte, 4> data{}; for (unsigned i = 0; i < 4; ++i) data[i] = std::byte((value >> (i * 8)) & 255); return Bytes(data); }
    bool BinaryWriter::U64(uint64_t value) { std::array<std::byte, 8> data{}; for (unsigned i = 0; i < 8; ++i) data[i] = std::byte((value >> (i * 8)) & 255); return Bytes(data); }
    bool BinaryWriter::F32(float value) { static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559); uint32_t bits; std::memcpy(&bits, &value, 4); return U32(bits); }
    bool BinaryWriter::String(std::string_view value) {
        if (value.size() > std::numeric_limits<uint32_t>::max()) { m_Error = "Binary string length overflows u32"; return false; }
        return U32(static_cast<uint32_t>(value.size())) && Bytes(std::as_bytes(std::span(value.data(), value.size())));
    }
    bool BinaryReader::Bytes(size_t size, std::span<const std::byte>& output) {
        if (!m_Error.empty()) return false;
        if (size > Remaining()) { m_Error = "Binary input is truncated"; return false; }
        output = m_Data.subspan(m_Offset, size); m_Offset += size; return true;
    }
    bool BinaryReader::U8(uint8_t& output) { std::span<const std::byte> bytes; if (!Bytes(1, bytes)) return false; output = std::to_integer<uint8_t>(bytes[0]); return true; }
    bool BinaryReader::U32(uint32_t& output) { std::span<const std::byte> bytes; if (!Bytes(4, bytes)) return false; uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= uint32_t(std::to_integer<uint8_t>(bytes[i])) << (i * 8); output = value; return true; }
    bool BinaryReader::U64(uint64_t& output) { std::span<const std::byte> bytes; if (!Bytes(8, bytes)) return false; uint64_t value = 0; for (unsigned i = 0; i < 8; ++i) value |= uint64_t(std::to_integer<uint8_t>(bytes[i])) << (i * 8); output = value; return true; }
    bool BinaryReader::F32(float& output) { uint32_t bits; if (!U32(bits)) return false; std::memcpy(&output, &bits, 4); return true; }
    bool BinaryReader::String(std::string& output, size_t maxBytes) {
        uint32_t length; if (!U32(length)) return false;
        if (length > maxBytes) { m_Error = "Binary string exceeds its limit"; return false; }
        std::span<const std::byte> bytes; if (!Bytes(length, bytes)) return false;
        try { std::string value; if (!bytes.empty()) value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size()); output = std::move(value); return true; }
        catch (const std::exception& exception) { m_Error = exception.what(); return false; }
    }
}
