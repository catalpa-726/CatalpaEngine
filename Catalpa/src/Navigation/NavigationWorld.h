#pragma once
#include "Navigation/NavMeshAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa {
    class CATALPA_API NavigationWorld final {
      public:
        NavigationWorld();
        ~NavigationWorld();
        NavigationWorld(const NavigationWorld&) = delete;
        NavigationWorld& operator=(const NavigationWorld&) = delete;
        bool Load(std::shared_ptr<const NavMeshAsset>, uint64_t version, std::string& error);
        void Unload() noexcept;
        bool FindPath(const NavigationPathRequest&, const CancellationToken&, NavigationPath&,
                      std::string& error) const;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
