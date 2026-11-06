#pragma once
#include "Scripting/Backends/IScriptBackend.h"
#include "Assets/AssetHandle.h"
#include "Scene/Components/ScriptComponent.h"

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
        };
        std::thread::id m_Owner;
        uint64_t m_Scene = 0;
        std::map<PersistentEntityId, Instance> m_Instances;
        std::vector<std::string> m_Diagnostics;
        std::shared_ptr<ScriptMemoryBudget> m_Memory;
    };
}
