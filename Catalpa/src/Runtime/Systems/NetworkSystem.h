#pragma once
#include "Networking/ReplicationService.h"
#include "Scene/EntityReference.h"
#include "Scene/Components/MeshRendererComponent.h"
namespace Catalpa {
	class Scene;
	class CATALPA_API NetworkSystem {
	public:
		explicit NetworkSystem(std::unique_ptr<ReplicationService>);
		/**
		 * @brief 为当前世界的模拟步更新复制实体与传输状态。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] tick 当前模拟时刻或步编号。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Tick(Scene&, uint64_t tick, std::string& error);
		void SetSpawnRenderer(MeshRendererComponent value) { m_Renderer = std::move(value); }
		ReplicationStats Stats() const noexcept { return m_Replication->Stats(); }
		/**
		 * @brief 关闭复制服务并清除远端实体的跟踪记录。
		 */
		void Stop() noexcept;

	private:
		std::unique_ptr<ReplicationService> m_Replication;
		std::map<StableId, EntityReference> m_Remote;
		std::map<StableId, uint64_t> m_Generations;
		std::set<StableId> m_Spawned;
		std::optional<MeshRendererComponent> m_Renderer;
		uint64_t m_World = 0;
	};
} /* 命名空间 Catalpa */
