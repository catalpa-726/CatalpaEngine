#pragma once
#include "Core/Base.h"
#include "Core/Serialization/Archive.h"
#include "ECS/World.h"
#include "Assets/AssetId.h"
#include "Scene/PersistentEntityId.h"
#include "Assets/AssetLoadResult.h"
namespace Catalpa {
    class Scene;
    CATALPA_API bool CaptureDomainComponents(const Scene&, EntityId, ArchiveValue::Object&, std::string&);
    CATALPA_API bool ValidateDomainComponents(const ArchiveValue::Object&, std::string&);
    CATALPA_API bool RestoreDomainComponents(Scene&, EntityId, const ArchiveValue::Object&, std::string&);
    CATALPA_API bool RestoreDomainComponents(World&, EntityId, const ArchiveValue::Object&, std::string&);
    CATALPA_API std::vector<AssetId> DomainComponentDependencies(const ArchiveValue::Object&);
    CATALPA_API bool CollectDomainComponentVersions(const World&, EntityId,
                                                    std::vector<AssetDependencyValue>&, std::string&);
    CATALPA_API bool BindDomainComponentVersions(World&, EntityId, std::span<const AssetDependencyValue>,
                                                 std::string&);
    CATALPA_API bool BindDomainComponentVersions(Scene&, EntityId, std::vector<AssetDependencyValue>,
                                                 std::string&);
    CATALPA_API bool SameDomainComponents(const ArchiveValue::Object&, const ArchiveValue::Object&);
    CATALPA_API bool RemapDomainEntityReferences(ArchiveValue::Object&,
                                                 const std::map<PersistentEntityId, PersistentEntityId>&,
                                                 std::string& error);
} // namespace Catalpa
