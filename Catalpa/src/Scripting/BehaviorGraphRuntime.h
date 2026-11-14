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
		/**
		 * @brief 在所属线程按模拟时刻推进场景中的行为实例。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] seconds 以秒计的时间。
		 */
		void Tick(Scene&, AssetManager&, double seconds);
		/**
		 * @brief 向行为实例发送具名事件。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] event 待投递给行为图实例的事件。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Emit(PersistentEntityId entity, std::string event, std::string& error);
		/**
		 * @brief 清空行为图实例及其运行记录。
		 */
		void Stop() noexcept;
		/**
		 * @brief 导出调试快照，可按需附带实例变量。
		 *
		 * @param[in] includeVariables include 变量。
		 *
		 * @return 运行实例的独立调试快照列表；非所属线程调用时返回空列表。
		 */
		std::vector<BehaviorDebugSnapshot> Snapshot(bool includeVariables = true) const;

	private:
		struct Pending {
			StableId node;
			double wake = 0;
			std::string event;
		};
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
		bool Prepare(Instance&, EntityReference, const BehaviorGraphComponent&, AssetVersionRef<BehaviorGraphAsset>,
		    std::string& error);
		bool Enqueue(Instance&, std::string_view event, std::string& error);
		bool Run(Scene&, Instance&, Pending, uint32_t& budget, std::string& error);
		std::thread::id m_Owner = std::this_thread::get_id();
		uint64_t m_Scene = 0;
		std::map<PersistentEntityId, Instance> m_Instances;
		std::optional<BehaviorDebugSnapshot> m_AdmissionFailure;
	};
}
