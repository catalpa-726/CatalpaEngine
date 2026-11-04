#pragma once
#include "Core/Base.h"
#include "Assets/AssetId.h"
#include "Materials/MaterialTypes.h"

namespace Catalpa {
    class ComponentRegistry;
    class AssetManager;
    // 在组合根注册作者身份/TRS/关系/mesh renderer的真实字段schema，底层ECS不依赖领域头。
    [[nodiscard]] CATALPA_API bool RegisterRuntimeComponentSchemas(ComponentRegistry& registry, std::string& error);
    // 在组合根装配域内loader；Runtime关闭顺序仍由宿主管理，不另建资产/GPU所有者。
    [[nodiscard]] CATALPA_API bool RegisterRuntimeAssetLoaders(AssetManager& manager, std::string& error);
    // 映射唯一BuiltinAssetIds常量；无效kind返回无效ID，不创建另一套内置资产编号。
    [[nodiscard]] CATALPA_API AssetId BuiltinMaterialId(BuiltinMaterialKind kind, bool definition = false) noexcept;
}
