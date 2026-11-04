#pragma once
#include "Core/Base.h"
#include "Materials/MaterialDefinition.h"

namespace Catalpa {
    // 固定definition版本和解析后的完整参数值；revision是实例编辑版本，不代替资产内容版本。
    struct MaterialInstance {
        AssetVersionRef<MaterialDefinition> definition;
        uint64_t revision = 1;
        std::vector<MaterialParameterOverride> parameters;
        MaterialRenderState renderState;
    };
    // 从默认值与显式覆盖构建新的const实例，任何失败均保留旧output。
    [[nodiscard]] CATALPA_API bool CreateMaterialInstance(const AssetVersionRef<MaterialDefinition>& definition,
        std::span<const MaterialParameterOverride> overrides, const MaterialStateOverrides& states,
        uint64_t revision, std::shared_ptr<const MaterialInstance>& output, std::string& error);
    // 在副本应用允许的覆盖并递增revision；旧定义/实例不被修改，溢出显式失败。
    [[nodiscard]] CATALPA_API bool UpdateMaterialInstance(const MaterialInstance& source,
        std::span<const MaterialParameterOverride> overrides, const MaterialStateOverrides& states,
        std::shared_ptr<const MaterialInstance>& output, std::string& error);
    // 查找完整解析值；返回指针在其所属不可变实例有效期间借用。
    [[nodiscard]] CATALPA_API const MaterialValue* FindMaterialParameter(const MaterialInstance& instance,
        std::string_view parameterId) noexcept;
}
