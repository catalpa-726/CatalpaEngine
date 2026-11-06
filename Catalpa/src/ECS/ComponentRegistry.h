#pragma once
#include "Core/Base.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
    // 组件持久字段schema，不持C++字节offset，不将RTTI当稳定名称。
    enum class ComponentFieldKind { Bool, Int, UInt, Number, String, Float2, Float3, Float4, StableId, StableIdArray, Object };
    struct ComponentFieldSchema {
        std::string id, displayName;
        ComponentFieldKind kind = ComponentFieldKind::Number;
        bool required = true;
        std::optional<double> minimum, maximum;
    };
    struct ComponentSchema {
        std::string typeId;
        uint32_t version = 1;
        std::vector<ComponentFieldSchema> fields;
        // Runtime可补充TRS等领域语义，ECS基础模块不反向包含任何领域组件。
        std::function<bool(const ArchiveValue&, std::string&)> validate;
    };
    class CATALPA_API ComponentRegistry {
    public:
        ComponentRegistry();
        ~ComponentRegistry();
        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;
        ComponentRegistry(ComponentRegistry&&) noexcept;
        ComponentRegistry& operator=(ComponentRegistry&&) noexcept;
        // 验证所有候选后以指针swap提交；重复或冲突注册失败时保留旧集合。
        [[nodiscard]] bool Register(ComponentSchema schema, std::string& error);
        [[nodiscard]] bool RegisterMany(std::span<const ComponentSchema> schemas, std::string& error);
        // 指针借用已有不可变schema；不允许调用方编辑注册内容。
        [[nodiscard]] const ComponentSchema* Find(std::string_view typeId) const noexcept;
        [[nodiscard]] std::span<const std::shared_ptr<const ComponentSchema>> GetSchemas() const noexcept;
        // 验证schemaVersion、字段类型/范围/未知字段，再调用已注册领域语义校验。
        [[nodiscard]] bool Validate(std::string_view typeId, const ArchiveValue& value, std::string& error) const;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
