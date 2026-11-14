#include "BoxDefaultEnvironment.h"

#include "AssetPipeline/AssetCooker.h"
#include "Assets/AssetManager.h"
#include "Core/PlatformServices.h"
#include "Lighting/EnvironmentAsset.h"
#include "Log/Log.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/EnvironmentComponent.h"

namespace Catalpa {
	namespace {
		const PersistentEntityId SkyEntity = PersistentEntityId::FromWords(0x434154424F58534BULL, 1);

		/**
		 * @brief 从可执行文件的位置查找开发样例项目。
		 * 开发样例按可执行文件的位置查找其项目，不依赖 cwd 或用户机器的盘符。
		 * 源根仍由项目配置声明，后续读取/输出和缓存全部交给共享 AssetCooker。
		 *
		 * @param[in] executable 搜索起点，即可执行文件所在目录。
		 * @param[out] project 成功时写入项目文件的规范路径。
		 * @param[out] error 失败时接收诊断消息。
		 *
		 * @return 找到样例项目并写入规范路径时为 true，未找到或路径检查失败时为 false。
		 */
		bool FindSampleProject(
		    const std::filesystem::path& executable, std::filesystem::path& project, std::string& error) {
			for (auto directory = executable; !directory.empty();) {
				const auto candidate = directory / "Projects" / "Sandbox" / "Sandbox.catalpaproj";
				std::error_code code;
				if (std::filesystem::is_regular_file(candidate, code)) {
					project = std::filesystem::canonical(candidate);
					return true;
				}
				const auto parent = directory.parent_path();
				if (parent == directory)
					break;
				directory = parent;
			}
			error = "Cannot locate the Sandbox project for the default HDR sky";
			return false;
		}

		Tools::AssetBuildResult BuildDefaultEnvironment(
		    const std::filesystem::path& executable, const CancellationToken& cancellation) {
			Tools::AssetBuildResult result;
			std::filesystem::path project;
			std::unique_ptr<Tools::ProjectSession> session;
			if (cancellation.IsCancellationRequested()) {
				result.error = "Default HDR sky build cancelled";
				return result;
			}
			if (!FindSampleProject(executable, project, result.error) ||
			    !Tools::LoadProject(project, session, result.error))
				return result;

			const auto archive = session->config.externalSourceRoots.find("archive");
			if (archive == session->config.externalSourceRoots.end()) {
				result.error = "Sandbox has no archive source root for the default HDR sky";
				return result;
			}

			Tools::AssetBuildRequest request;
			request.project = session->config;
			request.source = archive->second / "hdr" / "overcast_soil_puresky_8k.hdr";
			request.shaderRoot = executable / "Resources" / "Shaders";
			request.cancellation = cancellation;
			/* 8K RGBA32F 原图恰好 512 MiB；沿用 1.5 GiB 工作预算，原件保持只读。 */
			return Tools::AssetCooker::Build(std::move(request));
		}
	}

	struct BoxDefaultEnvironment::Task {
		Tools::AssetBuildResult result;
	};

	BoxDefaultEnvironment::~BoxDefaultEnvironment() {
		Stop();
	}

	bool BoxDefaultEnvironment::Start(
	    SceneManager& scenes, AssetManager& assets, JobSystem& jobs, SceneHandle scene, std::string& error) try {
		Stop();
		if (!scenes.GetScene(scene)) {
			error = "Default HDR sky requires the existing sample scene";
			return false;
		}
		std::filesystem::path executable;
		if (!GetExecutableDirectory(executable, error))
			return false;

		auto task = std::make_shared<Task>();
		JobHandle job;
		if (!jobs.Submit(
		        [task, executable = std::move(executable)](
		            const CancellationToken& token) { task->result = BuildDefaultEnvironment(executable, token); },
		        job, error))
			return false;

		m_Scenes = &scenes;
		m_Assets = &assets;
		m_Jobs = &jobs;
		m_Scene = scene;
		m_Task = std::move(task);
		m_Job = std::move(job);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	void BoxDefaultEnvironment::Fail(std::string_view error) {
		m_Loading = false;
		m_Environment = {};
		CAT_WARN("Default HDR sky unavailable: {0}", error);
	}

	void BoxDefaultEnvironment::Update() {
		if (!m_Scenes || (!m_Task && !m_Loading))
			return;
		Scene* scene = m_Scenes->GetScene(m_Scene);
		if (!scene) {
			/* 若场景代次已改变，直接丢弃结果，避免将它错误应用于当前活动场景。 */
			m_Job.Cancel();
			m_Loading = false;
			if (m_Task && m_Jobs->IsComplete(m_Job)) {
				m_Task.reset();
				m_Job = {};
			}
			return;
		}

		if (m_Task) {
			if (!m_Jobs->IsComplete(m_Job))
				return;
			/* 任务完成时的获取和释放同步保证工作线程已退出，此后可安全读取结果。 */
			const auto taskError = m_Job.GetError();
			auto result = std::move(m_Task->result);
			m_Task.reset();
			m_Job = {};
			if (!taskError.empty()) {
				Fail(taskError);
				return;
			}
			if (!result.success) {
				Fail(result.error);
				return;
			}

			const auto record = std::find_if(result.records.begin(), result.records.end(), [&](const auto& value) {
				return value.id == result.primary && value.type.ToString() == "environment";
			});
			if (record == result.records.end() || !record->dependencies.empty()) {
				Fail("Cook did not produce a self-contained environment asset");
				return;
			}
			AssetRegistry registry;
			std::string error;
			/* 只合入这张环境，不用旧的整份 manifest 撤下用户随后构建的其他资产。 */
			if (!registry.Replace({*record}, error) ||
			    !m_Assets->InstallCookedCatalog(result.cookedRoot, registry, error) ||
			    !m_Assets->Request(result.primary, error)) {
				Fail(error);
				return;
			}
			m_Environment = result.primary;
			m_Loading = true;
		}

		const auto status = m_Assets->GetStatus(m_Environment);
		if (status == AssetStatus::Failed || status == AssetStatus::Cancelled || status == AssetStatus::Unloaded) {
			Fail(m_Assets->GetDiagnostic(m_Environment));
			return;
		}
		if (status != AssetStatus::Ready || !scene->CanEditNow())
			return;

		bool hasEnvironment = false;
		const auto inspected = scene->GetWorld().ForEach([&](EntityId entity) -> WorldStatus {
			hasEnvironment |= scene->GetWorld().Has<EnvironmentComponent>(entity);
			return {};
		});
		if (!inspected.IsSuccess()) {
			Fail(inspected.message);
			return;
		}
		if (hasEnvironment || scene->Resolve(SkyEntity).HasValue()) {
			/* 等待期间用户已经设置环境或占用了样例实体身份时，保留用户的选择。 */
			m_Loading = false;
			return;
		}

		AssetVersionRef<EnvironmentAsset> environment;
		bool fallback = false;
		std::string error;
		if (!m_Assets->Acquire(m_Environment, environment, fallback, error) || fallback) {
			Fail(error.empty() ? "The exact HDR environment version is unavailable" : error);
			return;
		}
		auto applied = scene->QueueCreate(SkyEntity, "Sky environment", {});
		if (applied.IsSuccess())
			applied = scene->CommitPending();
		if (applied.IsSuccess()) {
			EnvironmentComponent component;
			component.environment = AssetHandle<EnvironmentAsset>{m_Environment};
			component.settings.drawBackground = true;
			applied = scene->QueueSetEnvironment(scene->Resolve(SkyEntity), component);
			if (applied.IsSuccess())
				applied = scene->CommitPending();
		}
		m_Loading = false;
		if (!applied.IsSuccess()) {
			/* 创建已提交而组件未提交时，清理本次空实体，不留下半个默认环境。 */
			const auto entity = scene->Resolve(SkyEntity);
			if (entity.HasValue()) {
				const auto removed = scene->QueueDestroySubtree(entity);
				if (removed.IsSuccess())
					(void)scene->CommitPending();
			}
			Fail(applied.message);
		}
	}

	void BoxDefaultEnvironment::Stop() noexcept {
		m_Job.Cancel();
		try {
			std::string error;
			if (m_Jobs && m_Job.HasValue() && !m_Jobs->Wait(m_Job, error))
				CAT_WARN("Default HDR sky shutdown: {0}", error);
		} catch (...) {
			/* 卸载不向 LayerStack 析构传播异常；JobSystem 随后仍会等待全部工作线程。 */
		}
		m_Job = {};
		m_Task.reset();
		m_Scene = {};
		m_Environment = {};
		m_Loading = false;
		m_Scenes = nullptr;
		m_Assets = nullptr;
		m_Jobs = nullptr;
	}
}
