#pragma once
#include "Materials/MaterialInstance.h"
#include "Geometry/VertexLayout.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
    struct ShaderMetadata;
    // 校验定义字段、稳定参数ID、默认值/范围、纹理语义和允许的状态策略。
    [[nodiscard]] CATALPA_API bool ValidateMaterialDefinition(const MaterialDefinition& definition, std::string& error);
    // 校验实际参数集合、固定定义版本、数值/纹理及状态约束，不创建GPU资源。
    [[nodiscard]] CATALPA_API bool ValidateMaterialInstance(const MaterialInstance& instance, std::string& error);
    // 对应最终shader metadata检查数值块、纹理绑定和显式顶点属性，不猜测C++内存ABI。
    [[nodiscard]] CATALPA_API bool ValidateMaterialShaderInterface(const MaterialDefinition& definition,
        const ShaderMetadata& metadata, const VertexLayout& vertices, std::string& error);
    // 供受检实例更新共用的单参数/状态校验，未知类型和不允许覆盖必须报错。
    [[nodiscard]] CATALPA_API bool ValidateMaterialValue(const MaterialParameter& parameter,
        const MaterialValue& value, std::string& error);
    // 在图像依赖解析后核对Texture2D维度与颜色/法线/数据语义，不修正图像或参数。
    [[nodiscard]] CATALPA_API bool ValidateMaterialTexture(const MaterialParameter& parameter,
        const ImageAsset& image, std::string& error);
    [[nodiscard]] CATALPA_API bool ResolveMaterialRenderState(const MaterialDefinition& definition,
        const MaterialRenderState& base, const MaterialStateOverrides& overrides,
        MaterialRenderState& output, std::string& error);
    // 材质payload通过统一有界JSON读入；成功发布const值，未知字段/版本均拒绝。
    [[nodiscard]] CATALPA_API bool ParseMaterialDefinition(std::string_view text,
        std::shared_ptr<const MaterialDefinition>& output, std::string& error, const ArchiveLimits& limits = {});
    // caller提供已经保活的确切definition版本，文档身份/版本必须一致，不在解析器内阻塞加载。
    [[nodiscard]] CATALPA_API bool ParseMaterialInstance(std::string_view text,
        const AssetVersionRef<MaterialDefinition>& definition,
        std::shared_ptr<const MaterialInstance>& output, std::string& error, const ArchiveLimits& limits = {});
    // 构造同一schema的内置Unlit/PBR/Error CPU定义；着色模型必须绑定各自的确切shader版本。
    [[nodiscard]] CATALPA_API bool CreateBuiltinMaterialDefinition(BuiltinMaterialKind kind, AssetId shaderId,
        std::shared_ptr<const MaterialDefinition>& output, std::string& error);
    // Error使用Unlit与品红色；caller提供统一内置身份，不在工厂重新编号。
    [[nodiscard]] CATALPA_API bool CreateBuiltinMaterialInstance(BuiltinMaterialKind kind,
        AssetId definitionId, uint64_t definitionVersion, AssetId shaderId,
        std::shared_ptr<const MaterialInstance>& output, std::string& error);
}
