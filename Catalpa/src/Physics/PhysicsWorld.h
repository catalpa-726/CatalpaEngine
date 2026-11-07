#pragma once
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
namespace Catalpa {
    // 单一 owner 线程拥有 SDK 世界。所有句柄包含世界代次；公共 API 只返回值。
    class CATALPA_API PhysicsWorld final {
      public:
        PhysicsWorld();
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        bool Initialize(const PhysicsWorldConfig&, std::string& error);
        void Shutdown() noexcept;
        bool CreateBody(const PhysicsBodyDesc&, const CollisionShapeAsset&, const PhysicsMaterialAsset&,
                        PhysicsBodyHandle&, std::string& error);
        bool DestroyBody(PhysicsBodyHandle, std::string& error);
        bool ReadBody(PhysicsBodyHandle, PhysicsBodyState&, std::string& error) const;
        bool SetPose(PhysicsBodyHandle, const PhysicsPose&, bool teleport, double dt, std::string& error);
        bool Step(double seconds, uint64_t tick, std::string& error);
        bool Raycast(Math::Vec3 origin, Math::Vec3 delta, PhysicsQueryHit&, std::string& error,
                     uint16_t layerMask = 0xffff, PhysicsBodyHandle ignore = {}) const;
        bool Sweep(const CollisionShapeAsset&, const PhysicsPose&, Math::Vec3 delta, PhysicsQueryHit&,
                   std::string& error, uint16_t layerMask = 0xffff, PhysicsBodyHandle ignore = {}) const;
        bool Overlap(const CollisionShapeAsset&, const PhysicsPose&, std::vector<PhysicsQueryHit>&,
                     std::string& error, uint16_t layerMask = 0xffff, size_t maxHits = 256) const;
        bool CreateCharacter(const CharacterSettings&, const PhysicsPose&, PhysicsBodyHandle&,
                             std::string& error, PhysicsBodyHandle replacing = {},
                             EntityReference owner = {});
        bool MoveCharacter(PhysicsBodyHandle, Math::Vec3 desiredVelocity, double seconds, CharacterMotion&,
                           std::string& error);
        bool TeleportCharacter(PhysicsBodyHandle, const PhysicsPose&, std::string& error);
        bool DestroyCharacter(PhysicsBodyHandle, std::string& error);
        void TakeEvents(std::vector<PhysicsContactEvent>&);
        uint64_t DroppedEvents() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
