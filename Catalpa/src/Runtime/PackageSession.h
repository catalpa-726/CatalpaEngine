#pragma once
#include "Assets/PackageReader.h"
#include "Scene/SceneHandle.h"

namespace Catalpa {
	class AssetManager;
	class SceneManager;
	enum class PackageSessionState { Idle, Loading, Running, Stopping, Failed };

	/* EngineRuntime 拥有，会话只借用既有资产和场景服务，不另建 World 或设备。 */
	class CATALPA_API PackageSession final {
	public:
		/**
		 * @brief 为资源包安装运行时目录并启动入口场景加载。
		 *
		 * @param[in] package 为本次运行提供打包资产的资源包。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] scenes 管理场景及其句柄的对象。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(std::shared_ptr<const PackageReader>, AssetManager&, SceneManager&, std::string& error);
		/**
		 * @brief 根据加载结果推进包会话，或继续完成已请求的关闭。
		 *
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] scenes 管理场景及其句柄的对象。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Pump(AssetManager&, SceneManager&, std::string& error);
		/**
		 * @brief 结束包会话，等待在途访问完成后清理挂载。
		 *
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] scenes 管理场景及其句柄的对象。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Stop(AssetManager&, SceneManager&, std::string& error);
		PackageSessionState State() const noexcept { return m_State; }
		SceneHandle Scene() const noexcept { return m_Scene; }
		AssetId EntryScene() const noexcept { return m_Package ? m_Package->EntryScene() : AssetId{}; }
		/**
		 * @brief 在运行时停机后释放包会话保留的资源快照。
		 * 整个 Runtime 已停止任务并清空场景后释放快照，避免析构时重新借用服务。
		 */
		void ResetAfterShutdown() noexcept;

	private:
		std::shared_ptr<const PackageReader> m_Package;
		SceneHandle m_Scene, m_Previous;
		PackageSessionState m_State = PackageSessionState::Idle;
	};
} /* 命名空间 Catalpa */
