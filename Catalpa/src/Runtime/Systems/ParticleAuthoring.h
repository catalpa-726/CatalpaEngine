#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
    CATALPA_API bool CaptureParticleComponents(const World&, EntityId,
                                               std::map<std::string, ArchiveValue, std::less<>>&,
                                               std::string& error);
    CATALPA_API bool RestoreParticleComponents(Scene&, EntityId,
                                               const std::map<std::string, ArchiveValue, std::less<>>&,
                                               std::string& error);
    CATALPA_API bool RestoreParticleComponents(World&, EntityId,
                                               const std::map<std::string, ArchiveValue, std::less<>>&,
                                               std::string& error);
    CATALPA_API bool ValidateParticleComponent(std::string_view, const ArchiveValue&, std::string& error);
    CATALPA_API std::vector<AssetId> ParticleComponentDependencies(std::string_view, const ArchiveValue&);
    CATALPA_API bool BindParticleComponentVersions(World&, EntityId, std::span<const AssetDependencyValue>,
                                                   std::string& error);
    CATALPA_API bool CollectParticleComponentVersions(const World&, EntityId,
                                                      std::vector<AssetDependencyValue>&, std::string& error);
} // namespace Catalpa
