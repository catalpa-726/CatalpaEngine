#pragma once
#include "Scripting/BehaviorGraphAsset.h"
#include "Scripting/BehaviorDebugSnapshot.h"
#include "Scripting/ScriptApi.h"
#include "Scene/Components/BehaviorGraphComponent.h"
#include "Assets/AssetManager.h"
#include <deque>

namespace Catalpa {
    class CATALPA_API BehaviorGraphRuntime {
      public:
        BehaviorGraphRuntime() = default;
        BehaviorGraphRuntime(const BehaviorGraphRuntime&) = delete;
        BehaviorGraphRuntime& operator=(const BehaviorGraphRuntime&) = delete;
        void Tick(Scene&, AssetManager&, double seconds);
        bool Emit(PersistentEntityId entity, std::string event, std::string& error);
        void Stop() noexcept;
        std::vector<BehaviorDebugSnapshot> Snapshot(bool includeVariables = true) const;
      private:
        struct Pending { StableId node; double wake = 0; std::string event; };
        struct Instance {
            EntityReference entity;
            BehaviorGraphComponent author;
            AssetVersionRef<BehaviorGraphAsset> asset;
            std::deque<Pending> queue;
            BehaviorDebugSnapshot debug;
            double seconds = 0;
            bool failed = false;
            uint64_t rejectedVersion = 0;
            BehaviorGraphComponent rejectedAuthor;
            std::string rejectionError;
        };
        bool Prepare(Instance&, EntityReference, const BehaviorGraphComponent&,
                     AssetVersionRef<BehaviorGraphAsset>, std::string& error);
        bool Enqueue(Instance&, std::string_view event, std::string& error);
        bool Run(Scene&, Instance&, Pending, uint32_t& budget, std::string& error);
        std::thread::id m_Owner = std::this_thread::get_id();
        uint64_t m_Scene = 0;
        std::map<PersistentEntityId, Instance> m_Instances;
        std::optional<BehaviorDebugSnapshot> m_AdmissionFailure;
    };
}
