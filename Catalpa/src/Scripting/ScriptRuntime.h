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
		/**
		 * @brief 根据场景与已加载的脚本版本推进实例生命周期。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] fixedSeconds 以秒计的时间。
		 */
		void Tick(Scene&, AssetManager&, double fixedSeconds);
		void BindJobs(JobSystem& jobs) noexcept { m_Jobs = &jobs; }
		/**
		 * @brief 发出事件。
		 *
		 * @param[in] target 目标 EntityReference。
		 * @param[in] channel 用于路由脚本事件的通道名称。
		 * @param[in] values 随事件一起传递的命名脚本值。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool EmitEvent(EntityReference, std::string_view channel, const ScriptValues&, std::string& error);
		struct SceneRequest {
			EntityReference requester;
			AssetId scene;
			uint64_t generation = 0;
		};
		/**
		 * @brief 取出场景请求。
		 *
		 * @return 取走的待处理场景请求列表。
		 */
		std::vector<SceneRequest> TakeSceneRequests();
		/**
		 * @brief 判断是否当前。
		 *
		 * @param[in] target 目标 EntityReference。
		 * @param[in] generation 用于识别过期引用的代数。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		bool IsCurrent(EntityReference, uint64_t generation) const noexcept;
		bool IsCurrentSceneRequest(const SceneRequest& request) const noexcept {
			return request.scene.HasValue() && IsCurrent(request.requester, request.generation);
		}
		/**
		 * @brief 在所属线程停止脚本运行并清理实例。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 */
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
		bool PrepareEvent(
		    EntityReference, uint64_t, std::string_view, const ScriptValues&, EventValue&, std::string& error);
		void CancelTasks(EntityReference, uint64_t generation = 0);
	};
}
