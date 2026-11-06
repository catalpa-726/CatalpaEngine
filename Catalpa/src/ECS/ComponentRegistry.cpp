#include "catpch.h"
#include "ECS/ComponentRegistry.h"
#include "Core/Id/StableId.h"

namespace Catalpa {
    struct ComponentRegistry::Storage {
        std::map<std::string, std::shared_ptr<const ComponentSchema>, std::less<>> byId;
        std::vector<std::shared_ptr<const ComponentSchema>> schemas;
    };
    namespace {
        // 稳定注册键限定ASCII，不进行大小写、空格或Unicode归一化。
        bool ValidKey(std::string_view key) noexcept {
            if (key.empty() || key.size() > 128 || key.front() < 'a' || key.front() > 'z') return false;
            for (const char c : key) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
            return true;
        }
        // JSON布尔值不能冒充数值；范围验证不改写归档中的原数字。
        bool Number(const ArchiveValue& value, double& output) noexcept {
            if (const auto* v = std::get_if<double>(&value.data)) output = *v;
            else if (const auto* v = std::get_if<int64_t>(&value.data)) output = static_cast<double>(*v);
            else if (const auto* v = std::get_if<uint64_t>(&value.data)) output = static_cast<double>(*v);
            else return false;
            return std::isfinite(output);
        }
        // 单字段检查同一注册schema的标量、向量和稳定身份约定。
        bool CheckField(const ComponentFieldSchema& field, const ArchiveValue& value) {
            const auto within = [&](double number) {
                return (!field.minimum || number >= *field.minimum) && (!field.maximum || number <= *field.maximum);
            };
            switch (field.kind) {
            case ComponentFieldKind::Bool: return std::holds_alternative<bool>(value.data);
            case ComponentFieldKind::Int: {
                if (const auto* v = std::get_if<int64_t>(&value.data)) return within(static_cast<double>(*v));
                if (const auto* v = std::get_if<uint64_t>(&value.data)) return *v <= uint64_t(std::numeric_limits<int64_t>::max()) && within(static_cast<double>(*v));
                return false;
            }
            case ComponentFieldKind::UInt: { uint64_t number = 0; return value.ReadUnsigned(number) && within(static_cast<double>(number)); }
            case ComponentFieldKind::Number: { double number = 0; return Number(value, number) && within(number); }
            case ComponentFieldKind::String: {
                const auto* v = std::get_if<std::string>(&value.data);
                return v && v->size() <= 65536 && v->find('\0') == std::string::npos;
            }
            case ComponentFieldKind::StableId: {
                const auto* v = std::get_if<std::string>(&value.data); StableId id;
                return v && StableId::TryParse(*v, id);
            }
            case ComponentFieldKind::StableIdArray: {
                const auto* v = std::get_if<ArchiveValue::Array>(&value.data);
                if (!v || v->size() > 65536) return false;
                for (const auto& item : *v) {
                    const auto* text = std::get_if<std::string>(&item.data); StableId id;
                    if (!text || !StableId::TryParse(*text, id)) return false;
                }
                return true;
            }
            case ComponentFieldKind::Object: {
                const auto* object=std::get_if<ArchiveValue::Object>(&value.data);
                return object && object->size()<=65536;
            }
            case ComponentFieldKind::Float2:
            case ComponentFieldKind::Float3:
            case ComponentFieldKind::Float4: {
                const size_t count = field.kind == ComponentFieldKind::Float2 ? 2 : field.kind == ComponentFieldKind::Float3 ? 3 : 4;
                const auto* v = std::get_if<ArchiveValue::Array>(&value.data);
                if (!v || v->size() != count) return false;
                for (const auto& item : *v) {
                    double number = 0;
                    if (!Number(item, number) || std::abs(number) > std::numeric_limits<float>::max()
                        || (number != 0 && static_cast<float>(number) == 0) || !within(number)) return false;
                }
                return true;
            }
            default: return false;
            }
        }
    }
    ComponentRegistry::ComponentRegistry() = default;
    ComponentRegistry::~ComponentRegistry() = default;
    ComponentRegistry::ComponentRegistry(ComponentRegistry&&) noexcept = default;
    ComponentRegistry& ComponentRegistry::operator=(ComponentRegistry&&) noexcept = default;

    bool ComponentRegistry::Register(ComponentSchema schema, std::string& error)
    {
        return RegisterMany(std::span<const ComponentSchema>(&schema, 1), error);
    }
    bool ComponentRegistry::RegisterMany(std::span<const ComponentSchema> schemas, std::string& error)
    {
        try {
            auto candidate = m_Storage ? std::make_unique<Storage>(*m_Storage) : std::make_unique<Storage>();
            if (schemas.size() > 4096 || candidate->schemas.size() > 4096 - schemas.size()) {
                error = "Component registry exceeds its schema limit"; return false;
            }
            for (const auto& schema : schemas) {
                if (!ValidKey(schema.typeId) || schema.version == 0 || schema.fields.empty() || schema.fields.size() > 1024) {
                    error = "Component schema ID/version/fields are invalid"; return false;
                }
                if (candidate->byId.contains(schema.typeId)) { error = "Duplicate component type: " + schema.typeId; return false; }
                std::set<std::string_view> fields;
                for (const auto& field : schema.fields) {
                    if (!ValidKey(field.id) || field.id == "schemaVersion" || !fields.insert(field.id).second
                        || field.displayName.size() > 1024 || field.displayName.find('\0') != std::string::npos
                        || static_cast<unsigned>(field.kind) > static_cast<unsigned>(ComponentFieldKind::Object)
                        || (field.minimum && !std::isfinite(*field.minimum)) || (field.maximum && !std::isfinite(*field.maximum))
                        || (field.minimum && field.maximum && *field.minimum > *field.maximum)) {
                        error = "Component field schema is invalid: " + field.id; return false;
                    }
                    if ((field.minimum || field.maximum) && (field.kind == ComponentFieldKind::Bool || field.kind == ComponentFieldKind::String
                        || field.kind == ComponentFieldKind::StableId || field.kind == ComponentFieldKind::StableIdArray || field.kind == ComponentFieldKind::Object)) {
                        error = "Nonnumeric component fields cannot declare numeric ranges"; return false;
                    }
                }
                auto stored = std::make_shared<const ComponentSchema>(schema);
                candidate->byId.emplace(schema.typeId, stored);
                candidate->schemas.push_back(std::move(stored));
            }
            m_Storage.swap(candidate); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    const ComponentSchema* ComponentRegistry::Find(std::string_view typeId) const noexcept
    {
        if (!m_Storage) return nullptr;
        const auto found = m_Storage->byId.find(typeId);
        return found != m_Storage->byId.end() ? found->second.get() : nullptr;
    }
    std::span<const std::shared_ptr<const ComponentSchema>> ComponentRegistry::GetSchemas() const noexcept
    {
        return m_Storage ? std::span<const std::shared_ptr<const ComponentSchema>>(m_Storage->schemas) : std::span<const std::shared_ptr<const ComponentSchema>>{};
    }
    bool ComponentRegistry::Validate(std::string_view typeId, const ArchiveValue& value, std::string& error) const
    {
        const auto* schema = Find(typeId);
        if (!schema) { error = "Component type is not registered"; return false; }
        const auto* object = std::get_if<ArchiveValue::Object>(&value.data);
        if (!object) { error = "Component document must be an object"; return false; }
        ArchiveError versionError;
        if (!RequireArchiveVersion(value, schema->version, versionError)) { error = versionError.ToString(); return false; }
        for (const auto& [name, item] : *object) {
            if (name == "schemaVersion") continue;
            const auto field = std::find_if(schema->fields.begin(), schema->fields.end(), [&](const auto& v) { return v.id == name; });
            if (field == schema->fields.end()) { error = "Unknown component field: " + name; return false; }
            if (!CheckField(*field, item)) { error = "Invalid component field type/range: " + name; return false; }
        }
        for (const auto& field : schema->fields) if (field.required && !object->contains(field.id)) {
            error = "Missing component field: " + field.id; return false;
        }
        try {
            if (schema->validate && !schema->validate(value, error)) return false;
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
