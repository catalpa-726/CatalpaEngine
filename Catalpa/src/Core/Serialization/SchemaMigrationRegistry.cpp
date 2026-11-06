#include "catpch.h"
#include "Core/Serialization/SchemaMigrationRegistry.h"

namespace Catalpa {
    bool SchemaMigrationRegistry::Register(std::string type, uint32_t from, Step step, std::string& error) {
        if (type.empty() || !step || from == UINT32_MAX || m_Steps.contains({type, from})) {
            error = "Invalid or duplicate schema migration";
            return false;
        }
        m_Steps.emplace(std::make_pair(std::move(type), from), std::move(step));
        error.clear();
        return true;
    }

    bool SchemaMigrationRegistry::Migrate(std::string_view type, uint32_t target, const ArchiveValue& source,
                                          ArchiveValue& output, std::string& error) const try {
        uint64_t version = 0;
        const auto* field = source.Find("schemaVersion");
        if (!field || !field->ReadUnsigned(version) || version > target || target - version > 64) {
            error = "Unsupported source schema version";
            return false;
        }
        ArchiveValue candidate = source;
        while (version < target) {
            const auto it = m_Steps.find({std::string(type), uint32_t(version)});
            if (it == m_Steps.end() || !it->second(candidate, error)) {
                if (error.empty()) error = "Missing migration from version " + std::to_string(version);
                return false;
            }
            auto* object = std::get_if<ArchiveValue::Object>(&candidate.data);
            if (!object) { error = "Migration did not produce an object"; return false; }
            (*object)["schemaVersion"] = ArchiveValue{++version};
        }
        output = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
