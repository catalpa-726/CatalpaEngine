#pragma once
#include "Assets/PackageReader.h"
#include "Scene/SceneHandle.h"

namespace Catalpa {
    class AssetManager;
    class SceneManager;
    enum class PackageSessionState { Idle, Loading, Running, Stopping, Failed };

    // EngineRuntime 拥有，会话只借用既有资产和场景服务，不另建 World 或设备。
    class CATALPA_API PackageSession final {
      public:
        bool Start(std::shared_ptr<const PackageReader>, AssetManager&, SceneManager&, std::string& error);
        bool Pump(AssetManager&, SceneManager&, std::string& error);
        bool Stop(AssetManager&, SceneManager&, std::string& error);
        PackageSessionState State() const noexcept {
            return m_State;
        }
        SceneHandle Scene() const noexcept {
            return m_Scene;
        }
        AssetId EntryScene() const noexcept {
            return m_Package ? m_Package->EntryScene() : AssetId{};
        }
        // 整个 Runtime 已停止任务并清空场景后释放快照，避免析构时重新借用服务。
        void ResetAfterShutdown() noexcept;

      private:
        std::shared_ptr<const PackageReader> m_Package;
        SceneHandle m_Scene, m_Previous;
        PackageSessionState m_State = PackageSessionState::Idle;
    };
} // namespace Catalpa
