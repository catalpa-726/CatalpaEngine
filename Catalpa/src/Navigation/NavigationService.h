#pragma once
#include "Navigation/NavigationWorld.h"
#include "Physics/PhysicsService.h"
#include "Scene/Components/NavigationAgentComponent.h"

namespace Catalpa {
    class JobSystem;
    CATALPA_API bool CaptureNavigationComponents(const Scene&, EntityId, DynamicComponentMap&, std::string&);
    CATALPA_API bool RestoreNavigationComponents(Scene&, EntityId, const DynamicComponentMap&, std::string&);
    CATALPA_API bool RestoreNavigationComponents(World&, EntityId, const DynamicComponentMap&, std::string&);
    CATALPA_API bool ValidateNavigationComponents(const DynamicComponentMap&, std::string&);
    CATALPA_API std::vector<AssetId> NavigationComponentDependencies(const DynamicComponentMap&);
    CATALPA_API WorldStatus SetNavigationAgent(Scene&, EntityId, const NavigationAgentComponent&);

    // 借用Runtime唯一Jobs。Worker只读取不可变tile并输出自有值路径，不访问World。
    class CATALPA_API NavigationService final {
      public:
        NavigationService();
        ~NavigationService();
        bool Tick(Scene&, AssetManager&, JobSystem&, double seconds, std::string& error);
        void Stop() noexcept;
        const std::vector<std::string>& Diagnostics() const noexcept;
        bool GetPath(PersistentEntityId, NavigationPath&) const;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
