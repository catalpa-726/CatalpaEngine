#pragma once
#include "Networking/ReplicationService.h"
#include "Scene/EntityReference.h"
#include "Scene/Components/MeshRendererComponent.h"
namespace Catalpa {
    class Scene;
    class CATALPA_API NetworkSystem {
      public:
        explicit NetworkSystem(std::unique_ptr<ReplicationService>);
        bool Tick(Scene&, uint64_t tick, std::string& error);
        void SetSpawnRenderer(MeshRendererComponent value) {
            m_Renderer = std::move(value);
        }
        ReplicationStats Stats() const noexcept {
            return m_Replication->Stats();
        }
        void Stop() noexcept;

      private:
        std::unique_ptr<ReplicationService> m_Replication;
        std::map<StableId, EntityReference> m_Remote;
        std::map<StableId, uint64_t> m_Generations;
        std::set<StableId> m_Spawned;
        std::optional<MeshRendererComponent> m_Renderer;
        uint64_t m_World = 0;
    };
} // namespace Catalpa
