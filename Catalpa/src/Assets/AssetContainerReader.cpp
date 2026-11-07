#include "catpch.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetCompression.h"
#include "Core/Serialization/BinaryArchive.h"
#include "Core/PlatformServices.h"
namespace Catalpa {
    namespace {
        constexpr std::array<std::byte, 8> Magic{std::byte{'C'},std::byte{'A'},std::byte{'T'},std::byte{'A'},std::byte{'S'},std::byte{'S'},std::byte{'E'},std::byte{'T'}};
        bool ValidHash(std::string_view value) { return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
        bool Validate(const AssetContainer& data, std::string& error) {
            if (!data.id.HasValue() || !data.type.HasValue() || data.version == 0 || data.payloadVersion == 0 || data.dependencies.size() > 1024) { error = "Invalid asset identity/type/version or dependency count"; return false; }
            std::set<AssetId> unique;
            for (const auto& dependency : data.dependencies) if (!dependency.id.HasValue() || dependency.id == data.id || dependency.version == 0 || !ValidHash(dependency.contentHash) || !unique.insert(dependency.id).second) {
                error = "Invalid, duplicate or self asset dependency"; return false;
            }
            return true;
        }
        bool Id(BinaryWriter& writer, AssetId id) { return writer.Bytes(std::as_bytes(std::span(id.value.bytes))); }
        bool Id(BinaryReader& reader, AssetId& id) { std::span<const std::byte> bytes; if (!reader.Bytes(16, bytes)) return false; for (size_t i = 0; i < 16; ++i) id.value.bytes[i] = std::to_integer<uint8_t>(bytes[i]); return true; }
    }
    bool ComputeAssetContentHash(std::span<const std::byte> input, std::string& output, std::string& error) {
        try {
            std::array<std::byte, 32> digest{}; if (!ComputeSha256(input, digest, error)) return false;
            constexpr char hex[] = "0123456789abcdef";
            std::string text; text.reserve(64);
            for (auto byte : digest) { auto value = std::to_integer<unsigned>(byte); text += hex[value >> 4]; text += hex[value & 15]; }
            output = std::move(text); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool WriteAssetContainer(const AssetContainer& data, std::vector<std::byte>& output, std::string& error, size_t maxBytes) {
        try {
            if (!Validate(data, error)) return false;
            std::array<std::byte, 32> digest; if (!ComputeSha256(data.payload, digest, error)) return false;
            BinaryWriter header(maxBytes);
            if (!header.Bytes(Magic) || !header.U32(1) || !header.U32(0x01020304) || !Id(header, data.id) || !header.U64(data.version)
                || !header.U32(data.payloadVersion) || !header.String(data.type.ToString()) || !header.U32(static_cast<uint32_t>(data.dependencies.size()))) { error = header.Error(); return false; }
            for (const auto& dependency : data.dependencies)
                if (!Id(header, dependency.id) || !header.U64(dependency.version) || !header.String(dependency.contentHash)) { error = header.Error(); return false; }
            // One block: count, kind(PAYL=1), offset, length, digest. Offset is from file start.
            const uint64_t payloadOffset = header.Data().size() + 4 + 4 + 8 + 8 + 32;
            if (!header.U32(1) || !header.U32(1) || !header.U64(payloadOffset) || !header.U64(data.payload.size()) || !header.Bytes(digest) || !header.Bytes(data.payload)) { error = header.Error(); return false; }
            output = header.Data(); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool AssetContainerReader::Read(std::span<const std::byte> input, AssetContainer& output, std::string& error, size_t maxPayloadBytes) {
        try {
            if (input.size() >= 12 && input[8] == std::byte{2} && input[9] == std::byte{0} &&
                input[10] == std::byte{0} && input[11] == std::byte{0})
                return ReadCompressedAssetContainer(input, output, error, maxPayloadBytes);
            BinaryReader reader(input); AssetContainer data;
            std::span<const std::byte> magic; uint32_t format = 0, order = 0, count = 0;
            if (!reader.Bytes(8, magic) || !std::equal(magic.begin(), magic.end(), Magic.begin()) || !reader.U32(format) || format != 1 || !reader.U32(order) || order != 0x01020304) { error = "Unsupported or truncated asset container header"; return false; }
            std::string type;
            if (!Id(reader, data.id) || !reader.U64(data.version) || !reader.U32(data.payloadVersion) || !reader.String(type, 128) || !AssetType::TryParse(type, data.type) || !reader.U32(count) || count > 1024 || count > reader.Remaining() / 92) { error = "Invalid asset container metadata"; return false; }
            data.dependencies.resize(count);
            for (auto& dependency : data.dependencies)
                if (!Id(reader, dependency.id) || !reader.U64(dependency.version) || !reader.String(dependency.contentHash, 64)) { error = "Truncated asset dependency table"; return false; }
            uint32_t blocks = 0, kind = 0; uint64_t offset = 0, length = 0; std::span<const std::byte> digest;
            if (!reader.U32(blocks) || blocks != 1 || !reader.U32(kind) || kind != 1 || !reader.U64(offset) || !reader.U64(length) || !reader.Bytes(32, digest)) { error = "Unsupported or truncated asset block table"; return false; }
            if (offset != input.size() - reader.Remaining() || length > maxPayloadBytes || length != reader.Remaining()) { error = "Asset block offset/length is out of bounds or contains trailing bytes"; return false; }
            std::span<const std::byte> payload; if (!reader.Bytes(static_cast<size_t>(length), payload) || !reader.AtEnd() || !Validate(data, error)) return false;
            std::array<std::byte, 32> actual; if (!ComputeSha256(payload, actual, error)) return false;
            if (!std::equal(digest.begin(), digest.end(), actual.begin())) { error = "Asset payload SHA-256 mismatch"; return false; }
            data.payload.assign(payload.begin(), payload.end()); output = std::move(data); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
