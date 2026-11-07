#pragma once
#include "Networking/LoopbackSettings.h"
#include "Runtime/Systems/NetworkSystem.h"
#include "Scene/SceneHandle.h"
#include "Panels/PanelText.h"
namespace Catalpa {
    class Application;
}
namespace Catalpa::Editor {
    // 一个 Box 内两份独立 World，通过真实字节协议与 Loopback session 同步。
    class NetworkDiagnosticsPanel {
      public:
        void Draw(Application&, const PanelText&, std::string& error);
        void Close(Application&);

      private:
        bool Start(Application&, std::string& error);
        bool Spawn(Application&, std::string& error);
        bool Tick(Application&, double elapsed, std::string& error);
        SceneHandle m_ServerScene, m_ClientScene;
        std::unique_ptr<NetworkSystem> m_Server, m_Client;
        MeshRendererComponent m_Visual;
        PersistentEntityId m_Object;
        StableId m_NetworkId;
        LoopbackSettings m_Settings;
        uint64_t m_Tick = 0, m_Generation = 0;
        double m_LastTime = 0, m_Accumulator = 0, m_DroppedSeconds = 0;
        bool m_Animate = true;
        bool m_LoadedSettings = false;
    };
} // namespace Catalpa::Editor
