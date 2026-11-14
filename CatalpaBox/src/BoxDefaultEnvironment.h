#pragma once

#include "Core/Jobs/JobHandle.h"
#include "Scene/SceneHandle.h"
#include "Assets/AssetId.h"

namespace Catalpa {
	class SceneManager;
	class AssetManager;
	class JobSystem;

	/* Box 默认样例的天空装配；只持有指定场景句柄，不给其他作者场景隐式添加环境。 */
	class BoxDefaultEnvironment final {
	public:
		BoxDefaultEnvironment() = default;
		~BoxDefaultEnvironment();
		BoxDefaultEnvironment(const BoxDefaultEnvironment&) = delete;
		BoxDefaultEnvironment& operator=(const BoxDefaultEnvironment&) = delete;

		/**
		 * @brief 为已有样例场景提交后台 HDR 环境构建任务。
		 *
		 * @param[in] scenes 按句柄验证样例场景身份的场景管理器。
		 * @param[in] assets 随后安装构建目录并加载环境版本的资源管理器。
		 * @param[in] jobs 运行构建任务的作业系统。
		 * @param[in] scene 本次环境允许写入的目标场景句柄。
		 * @param[out] error 失败时接收诊断消息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(SceneManager&, AssetManager&, JobSystem&, SceneHandle, std::string& error);
		/**
		 * @brief 轮询构建和加载结果，在目标场景可编辑且还没有环境时安装默认天空。
		 */
		void Update();
		/**
		 * @brief 取消并等待天空环境的烘焙任务，再释放所借用的运行时资源。
		 * Layer 卸载先取消并等待自己的 Cook 任务，再释放 Runtime 借用。
		 */
		void Stop() noexcept;

	private:
		struct Task;
		void Fail(std::string_view error);

		SceneManager* m_Scenes = nullptr;
		AssetManager* m_Assets = nullptr;
		JobSystem* m_Jobs = nullptr;
		SceneHandle m_Scene;
		std::shared_ptr<Task> m_Task;
		JobHandle m_Job;
		AssetId m_Environment;
		bool m_Loading = false;
	};
}
