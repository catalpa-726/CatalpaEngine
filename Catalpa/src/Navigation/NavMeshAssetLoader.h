#pragma once
#include "Core/Base.h"
#include <string>
namespace Catalpa {
    class AssetManager;
    CATALPA_API bool RegisterNavigationAssetLoader(AssetManager&, std::string& error);
} // namespace Catalpa
