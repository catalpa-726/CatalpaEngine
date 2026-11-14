#pragma once
#include "Networking/LoopbackSettings.h"
#include "Runtime/Systems/NetworkSystem.h"
#include "Scene/SceneHandle.h"
#include "Panels/PanelText.h"
namespace Catalpa {
	class Application;
}
namespace Catalpa::Editor {
	/* 一个 Box 内两份独立 World，通过真实字节协议与 Loopback session 同步。 */
	class NetworkDiagnosticsPanel {
	public:
		/**
		 * @brief 显示 Loopback 会话控制、复制状态和诊断信息。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Draw(Application&, const PanelText&, std::string& error);
		/**
		 * @brief 关闭本地网络会话并清理关联场景。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 */
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
} /* 命名空间 Catalpa::Editor */
