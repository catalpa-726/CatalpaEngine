#pragma once
#include "Core/Serialization/Archive.h"
#include "Scene/Scene.h"
namespace Catalpa {
    CATALPA_API bool CaptureNetworkComponents(const World&, EntityId,
                                              std::map<std::string, ArchiveValue, std::less<>>&,
                                              std::string& error);
    CATALPA_API bool RestoreNetworkComponents(Scene&, EntityId,
                                              const std::map<std::string, ArchiveValue, std::less<>>&,
                                              std::string& error);
    CATALPA_API bool RestoreNetworkComponents(World&, EntityId,
                                              const std::map<std::string, ArchiveValue, std::less<>>&,
                                              std::string& error);
    CATALPA_API bool ValidateNetworkComponent(std::string_view, const ArchiveValue&, std::string& error);
} // namespace Catalpa
