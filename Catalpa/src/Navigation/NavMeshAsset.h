#pragma once
#include "Navigation/NavigationTypes.h"

namespace Catalpa {
    // 单个不可变 Detour tile，格式钉死到当前32-bit-polyref、小端构建。
    // SDK 对象只在私有 backend 内创建，资产和公共API均只传自有值。
    struct NavMeshAsset {
        NavigationBuildSettings settings;
        std::vector<std::byte> tile;
    };
    CATALPA_API bool ValidateNavMeshAsset(const NavMeshAsset&, std::string& error);
    CATALPA_API bool ReadNavMeshAsset(std::string_view, NavMeshAsset&, std::string& error);
    CATALPA_API bool WriteNavMeshAsset(const NavMeshAsset&, std::string&, std::string& error);
    CATALPA_API bool ReadNavigationSource(std::string_view, NavigationBuildInput&, std::string& error);
    CATALPA_API bool ReadNavigationSource(std::string_view, const NavigationBuildSettings& defaults,
                                          NavigationBuildInput&, std::string& error);
    CATALPA_API bool WriteNavigationSource(const NavigationBuildInput&, std::string&, std::string& error);
} // namespace Catalpa
