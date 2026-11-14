#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Core/Jobs/JobHandle.h"
namespace Catalpa {
	struct JobSystemConfig {
		uint32_t workerCount = 2;
		size_t maxPending = 64;
	};
	/*
	 * 单个 enkiTS 调度器将作业绑定到工作线程；界面线程轮询时不执行这些作业。
	 * 启动、提交、回收、等待及关闭须在启动线程调用。任意线程均可请求取消。
	 * 在其他线程销毁仍在运行的调度器会终止程序。
	 */
	class CATALPA_API JobSystem {
	public:
		JobSystem();
		~JobSystem();
		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;
		/**
		 * @brief 按给定工作线程数量和容量预算启动作业系统。
		 *
		 * @param[in] config 线程与作业容量配置。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(const JobSystemConfig&, std::string& error);
		/**
		 * @brief 在作业系统所属线程提交可取消任务，并为其绑定工作线程。
		 * 在调度器所属线程接受作业，按轮询顺序交给固定工作线程；容量耗尽时拒绝提交。
		 *
		 * @param[in] work 接收取消令牌的工作回调。
		 * @param[out] output 成功时写入可等待或取消的句柄。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 任务已交给调度器且句柄已发布时为 true；线程、容量或分配不满足要求时为 false。
		 */
		bool Submit(std::function<void(const CancellationToken&)> work, JobHandle& output, std::string& error);
		/**
		 * @brief 收集已完成的。
		 */
		void CollectCompleted();
		/**
		 * @brief 在所属线程核对作业及底层任务的完成状态。
		 * 由调度器所属线程查询，同时检查作业句柄及底层任务，避免回调返回前提前回收。
		 *
		 * @param[in] handle 待在所属线程核查完成状态的作业句柄。
		 *
		 * @return 句柄已完成且相应底层任务已结束或已回收时为 true；其他线程的查询返回 false。
		 */
		[[nodiscard]] bool IsComplete(const JobHandle&) const noexcept;
		/**
		 * @brief 等待指定作业完成并报告执行结果。
		 *
		 * @param[in] handle 要等待的已提交作业。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Wait(const JobHandle&, std::string& error);
		/**
		 * @brief 取消剩余作业，等待工作线程退出并释放调度器。
		 * 须在启动调度器的线程调用。先发送取消请求，等工作线程退出后才释放任务。
		 */
		void Shutdown() noexcept;
		[[nodiscard]] size_t GetPendingCount() const noexcept;
		[[nodiscard]] bool IsRunning() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
