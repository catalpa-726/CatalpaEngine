#pragma once
#include "catpch.h"
#include "Core/Base.h"
namespace Catalpa {
    // Explicit little-endian scalar protocol; never serializes native structure padding or pointers.
    class CATALPA_API BinaryWriter {
    public:
        explicit BinaryWriter(size_t maxBytes = 64 * 1024 * 1024) : m_Limit(maxBytes) {}
        bool U8(uint8_t); bool U32(uint32_t); bool U64(uint64_t); bool F32(float);
        bool Bytes(std::span<const std::byte>); bool String(std::string_view);
        [[nodiscard]] const std::vector<std::byte>& Data() const noexcept { return m_Data; }
        [[nodiscard]] const std::string& Error() const noexcept { return m_Error; }
    private:
        size_t m_Limit;
        std::vector<std::byte> m_Data;
        std::string m_Error;
    };
    class CATALPA_API BinaryReader {
    public:
        explicit BinaryReader(std::span<const std::byte> bytes) : m_Data(bytes) {}
        bool U8(uint8_t&); bool U32(uint32_t&); bool U64(uint64_t&); bool F32(float&);
        bool Bytes(size_t, std::span<const std::byte>&); bool String(std::string&, size_t maxBytes = 4096);
        [[nodiscard]] bool AtEnd() const noexcept { return m_Offset == m_Data.size() && m_Error.empty(); }
        [[nodiscard]] size_t Remaining() const noexcept { return m_Data.size() - m_Offset; }
        [[nodiscard]] const std::string& Error() const noexcept { return m_Error; }
    private:
        std::span<const std::byte> m_Data;
        size_t m_Offset = 0;
        std::string m_Error;
    };
}
