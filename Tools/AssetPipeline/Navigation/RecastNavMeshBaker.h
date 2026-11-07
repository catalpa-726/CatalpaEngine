#pragma once
#include "Navigation/NavMeshAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
    class RecastNavMeshBaker final {
      public:
        // 首版单tile、Y-up米制；不简化或覆盖源几何，只产生独立导航数据。
        static bool Bake(const NavigationBuildInput&, const CancellationToken&, NavMeshAsset&,
                         std::string& error);
    };
} // namespace Catalpa::Tools
