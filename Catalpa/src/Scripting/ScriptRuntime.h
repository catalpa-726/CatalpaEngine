#pragma once
#include "Scripting/Backends/IScriptBackend.h"
#include "Assets/AssetHandle.h"
#include "Scene/Components/ScriptComponent.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Memory/MemoryBudget.h"
#include <list>

namespace Catalpa {
    class AssetManager;
    struct ScriptMemoryBudget;
    class CATALPA_API ScriptRuntime {
      public:
        ScriptRuntime();
        ~ScriptRuntime();
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        void Tick(Scene&, AssetManager&, double fixedSeconds);
        void BindJobs(JobSystem& jobs) noexcept {
            m_Jobs = &jobs;
        }
        bool EmitEvent(EntityReference, std::string_view channel, const ScriptValues&, std::string& error);
        struct SceneRequest {
            EntityReference requester;
            AssetId scene;
            uint64_t generation = 0;
        };
        std::vector<SceneRequest> TakeSceneRequests();
        bool IsCurrent(EntityReference, uint64_t generation) const noexcept;
        bool IsCurrentSceneRequest(const SceneRequest& request) const noexcept {
            return request.scene.HasValue() && IsCurrent(request.requester, request.generation);
        }
        void Stop(Scene* scene = nullptr);
        const std::vector<std::string>& Diagnostics() const noexcept { return m_Diagnostics; }
      private:
        struct Instance {
            EntityReference entity;
            ScriptComponent author;
            AssetVersionRef<ScriptAsset> asset;
            std::unique_ptr<IScriptBackend> backend;
            uint64_t rejectedVersion = 0;
            ScriptComponent rejectedAuthor;
            bool failed = false;
            uint64_t generation = 0;
            std::string diagnostic;
        };
        std::thread::id m_Owner;
        uint64_t m_Scene = 0;
        std::map<PersistentEntityId, Instance> m_Instances;
        std::vector<std::string> m_Diagnostics;
        std::shared_ptr<ScriptMemoryBudget> m_Memory;
        struct EventValue {
            EntityReference target;
            uint64_t generation = 0;
            std::string channel;
            ScriptValues values;
            MemoryReservation reservation;
        };
        struct TaskResult {
            std::mutex mutex;
            ScriptValues values;
            std::string error;
            bool success = false;
        };
        struct Task {
            uint64_t ticket = 0, generation = 0, submittedTick = 0;
            EntityReference target;
            std::string callback;
            JobHandle job;
            std::shared_ptr<TaskResult> result;
        };
        JobSystem* m_Jobs = nullptr;
        uint64_t m_NextGeneration = 1, m_NextTask = 1;
        uint64_t m_Tick = 0;
        MemoryBudget m_EventBudget{4 * 1024 * 1024};
        std::list<EventValue> m_Events;
        std::vector<Task> m_Tasks;
        std::vector<SceneRequest> m_SceneRequests;
        ScriptApi MakeApi(Scene&, EntityReference, bool reload = false);
        bool PublishApi(ScriptApi&, uint64_t generation, std::string& error);
        bool QueueEvent(EntityReference, uint64_t, std::string_view, const ScriptValues&, std::string& error);
        bool PrepareEvent(EntityReference, uint64_t, std::string_view, const ScriptValues&, EventValue&,
                          std::string& error);
        void CancelTasks(EntityReference, uint64_t generation = 0);
    };
}
