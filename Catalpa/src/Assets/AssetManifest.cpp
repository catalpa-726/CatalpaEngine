#include "catpch.h"
#include "Assets/AssetManifest.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
    namespace {
        bool Complete(const AssetRegistry& registry, std::string& error) {
            if (registry.GetRecords().size() > 65536 || !registry.GetMissingDependencies().empty()) { error = "Runtime manifest has too many entries or unresolved dependencies"; return false; }
            for (const auto& record : registry.GetRecords()) if (record.locator.kind != AssetLocationKind::Cooked || record.contentHash.size() != 64 || record.dependencies.size() > 1024) {
                error = "Runtime manifest requires cooked locators, expected container digests and bounded dependencies"; return false;
            }
            return true;
        }
    }
    bool AssetManifest::Write(const AssetRegistry& registry, std::vector<std::byte>& output, std::string& error) {
        try {
            if (!Complete(registry, error)) return false;
            BinaryWriter writer;
            if (!writer.String("CATALPA-MANIFEST") || !writer.U32(1) || !writer.U32(static_cast<uint32_t>(registry.GetRecords().size()))) { error = writer.Error(); return false; }
            for (const auto& record : registry.GetRecords()) {
                if (!writer.String(record.id.ToString()) || !writer.String(record.type.ToString()) || !writer.U64(record.version) || !writer.String(record.locator.path.ToString())
                    || !writer.String(record.contentHash) || !writer.U32(static_cast<uint32_t>(record.dependencies.size()))) { error = writer.Error(); return false; }
                for (auto id : record.dependencies) if (!writer.String(id.ToString())) { error = writer.Error(); return false; }
            }
            output = writer.Data(); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool AssetManifest::Read(std::span<const std::byte> bytes, AssetRegistry& output, std::string& error) {
        try {
            if (bytes.size() > 64 * 1024 * 1024) { error = "Manifest exceeds byte limit"; return false; }
            BinaryReader reader(bytes); std::string magic; uint32_t version, count;
            if (!reader.String(magic, 32) || magic != "CATALPA-MANIFEST" || !reader.U32(version) || version != 1 || !reader.U32(count) || count > 65536) { error = "Invalid runtime manifest header"; return false; }
            std::vector<AssetRecord> records; records.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                AssetRecord record; std::string id, type, path; uint32_t dependencies;
                if (!reader.String(id, 36) || !AssetId::TryParse(id, record.id) || !reader.String(type, 128) || !AssetType::TryParse(type, record.type) || !reader.U64(record.version)
                    || !reader.String(path, 32760) || !VirtualPath::Parse(path, record.locator.path, error) || !reader.String(record.contentHash, 64) || !reader.U32(dependencies) || dependencies > 1024) {
                    error = "Invalid runtime manifest entry"; return false;
                }
                record.locator.kind = AssetLocationKind::Cooked;
                for (uint32_t j = 0; j < dependencies; ++j) {
                    AssetId dependency;
                    if (!reader.String(id, 36) || !AssetId::TryParse(id, dependency)) { error = "Invalid manifest dependency ID"; return false; }
                    record.dependencies.push_back(dependency);
                }
                records.push_back(std::move(record));
            }
            if (!reader.AtEnd()) { error = "Manifest has trailing bytes"; return false; }
            AssetRegistry candidate;
            if (!candidate.Replace(records, error) || !Complete(candidate, error)) return false;
            output = std::move(candidate); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
