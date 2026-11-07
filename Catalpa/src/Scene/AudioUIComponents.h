#pragma once
#include "Core/Serialization/Archive.h"
#include "Assets/AssetId.h"
#include "Assets/AssetLoadResult.h"
#include "Scene/Scene.h"
namespace Catalpa {
    CATALPA_API bool CaptureAudioUIComponents(const Scene&, EntityId, ArchiveValue::Object&,
                                              std::string& error);
    CATALPA_API bool ValidateAudioUIComponent(std::string_view, const ArchiveValue&, std::string& error);
    CATALPA_API bool AudioUIComponentDependencies(std::string_view, const ArchiveValue&,
                                                  std::vector<AssetId>&, std::string& error);
    CATALPA_API WorldStatus RestoreAudioUIComponent(Scene&, EntityId, std::string_view, const ArchiveValue&,
                                                    std::string& error);
    CATALPA_API WorldStatus RestoreAudioUIComponent(World&, EntityId, std::string_view, const ArchiveValue&,
                                                    std::string& error);
    CATALPA_API bool BindAudioUIComponentVersions(World&, EntityId, std::span<const AssetDependencyValue>,
                                                  std::string& error);
    CATALPA_API bool CollectAudioUIComponentVersions(const World&, EntityId,
                                                     std::vector<AssetDependencyValue>&, std::string& error);
} // namespace Catalpa
