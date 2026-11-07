#pragma once
#include "Physics/PhysicsWorld.h"
#include "Core/Serialization/Archive.h"
#include "Scene/Components/RigidBodyComponent.h"
#include "Scene/Components/ColliderComponent.h"
#include "Scene/Components/CharacterControllerComponent.h"
namespace Catalpa {
    class Scene;
    class AssetManager;
    using DynamicComponentMap = std::map<std::string, ArchiveValue, std::less<>>;
    CATALPA_API bool CapturePhysicsComponents(const Scene&, EntityId, DynamicComponentMap&,
                                              std::string& error);
    CATALPA_API bool RestorePhysicsComponents(Scene&, EntityId, const DynamicComponentMap&,
                                              std::string& error);
    // Scene恢复事务专用：调用者负责整个实体的失败回滚。
    CATALPA_API bool RestorePhysicsComponents(World&, EntityId, const DynamicComponentMap&,
                                              std::string& error);
    CATALPA_API bool ValidatePhysicsComponents(const DynamicComponentMap&, std::string& error);
    CATALPA_API std::vector<AssetId> PhysicsComponentDependencies(const DynamicComponentMap&);
    CATALPA_API WorldStatus SetRigidBody(Scene&, EntityId, const RigidBodyComponent&);
    CATALPA_API WorldStatus SetCollider(Scene&, EntityId, const ColliderComponent&);
    CATALPA_API WorldStatus SetCharacterController(Scene&, EntityId, const CharacterControllerComponent&);

    // Runtime 拥有。场景切换会清空旧SDK关联，场景/Jobs本身仍由既有Runtime拥有。
    class CATALPA_API PhysicsService final {
      public:
        PhysicsService();
        ~PhysicsService();
        // 世界创建前配置；坏配置不改变既有候选，活动世界不接受原地重配。
        bool ConfigureFromJson(std::string_view, std::string& error);
        bool Tick(Scene&, AssetManager&, double seconds, uint64_t tick, std::string& error);
        bool EnsureWorld(Scene&, std::string& error);
        void Stop() noexcept;
        PhysicsWorld* World() noexcept;
        void TakeEvents(std::vector<PhysicsContactEvent>&);
        const std::vector<std::string>& Diagnostics() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
