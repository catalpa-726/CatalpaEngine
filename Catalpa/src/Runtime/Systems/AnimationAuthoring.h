#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
#include "Assets/AssetId.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
    CATALPA_API bool CaptureAnimationComponents(const World&, EntityId,
                                                std::map<std::string, ArchiveValue, std::less<>>&,
                                                std::string& error);
    CATALPA_API bool RestoreAnimationComponents(Scene&, EntityId,
                                                const std::map<std::string, ArchiveValue, std::less<>>&,
                                                std::string& error);
    // 供已有 Scene 恢复事务调用；失败由外层子树事务回滚，不再排入下一批命令。
    CATALPA_API bool RestoreAnimationComponents(World&, EntityId,
                                                const std::map<std::string, ArchiveValue, std::less<>>&,
                                                std::string& error);
    CATALPA_API bool ValidateAnimationComponent(std::string_view key, const ArchiveValue&,
                                                std::string& error);
    CATALPA_API std::vector<AssetId> AnimationComponentDependencies(std::string_view key,
                                                                    const ArchiveValue&);
    CATALPA_API bool BindAnimationComponentVersions(World&, EntityId, std::span<const AssetDependencyValue>,
                                                    std::string& error);
    CATALPA_API bool CollectAnimationComponentVersions(const World&, EntityId,
                                                       std::vector<AssetDependencyValue>&,
                                                       std::string& error);
} // namespace Catalpa
