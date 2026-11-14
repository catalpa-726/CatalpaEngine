#pragma once
#include "Panels/PanelText.h"
#include "Core/IO/FileWatcher.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetDependencyGraph.h"
#include "Scene/SceneManager.h"
#include "Scene/ModelInstantiation.h"
#include "Camera/CameraLens.h"
#include "Geometry/Bounds.h"

namespace Catalpa::Editor {
	/**
	 * @brief 按模型包围盒和实际输出宽高计算预览相机的位姿与裁剪面。
	 * 以主窗口的像素宽高为模型包围盒计算预览相机和裁剪面，失败时保留输出。
	 *
	 * @param[in] bounds 要完整纳入预览的模型空间范围。
	 * @param[in] viewport 预览目标的非零像素宽高。
	 * @param[out] lens 成功时接收适配模型尺寸和视口比例的镜头参数。
	 * @param[out] placement 成功时接收取景相机的位姿变换。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 算出的相机位姿和镜头参数通过投影校验时为 true，失败时为 false。
	 */
	bool FitModelPreviewCamera(const Aabb& bounds, const CameraViewport& viewport, CameraLens& lens,
	    Math::TransformTRS& placement, std::string& error);

	/* 工作区拥有任务；关闭窗口只隐藏。工作线程仅持值请求/共享结果，不捕获this。 */
	class BuildPanel final {
	public:
		BuildPanel(JobSystem&, AssetManager&, SceneManager&);
		~BuildPanel();
		BuildPanel(const BuildPanel&) = delete;
		BuildPanel& operator=(const BuildPanel&) = delete;
		/**
		 * @brief 提交指定的资源构建请求。
		 *
		 * @param[in] request 描述本次请求的 Tools::AssetBuildRequest。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(Tools::AssetBuildRequest, std::string& error);
		/**
		 * @brief 轮询构建结果并推进资源加载或预览。
		 *
		 * @param[in] viewport 用于确定取景宽高比的像素区域。
		 */
		void Update(const CameraViewport& viewport);
		/**
		 * @brief 向正在进行的构建任务请求取消。
		 */
		void Cancel();
		/**
		 * @brief 关闭项目。
		 */
		void CloseProject();
		/**
		 * @brief 显示构建诊断、产物以及可用的实例化或预览操作。
		 *
		 * @param[in] visible 控制可见性的标志。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Draw(bool& visible, const PanelText&);
		bool Busy() const noexcept;
		AssetId SelectedAsset() const noexcept { return m_AssetsReady ? m_Selected : AssetId{}; }
		AssetId TakeOpenAsset() noexcept { return std::exchange(m_OpenAsset, AssetId{}); }
		const auto& Recipe() const noexcept { return m_Recipe; }

		const Tools::AssetBuildResult& Result() const { return m_Result; }

	private:
		struct TaskState {
			std::shared_ptr<const Tools::AssetBuildRequest> recipe;
			std::mutex mutex;
			std::string phase;
			size_t done = 0, total = 1;
			Tools::AssetBuildResult result;
		};

		void Fail(std::string);
		void ReleaseProjectCache(); /* 释放本面板请求的CPU缓存，固定场景快照仍保活自己的版本。 */
		void FinishBuild();
		void UpdateLoad();
		bool InstantiateSelectedModel(
		    Scene&, std::shared_ptr<ModelInstantiationResult>&, const CameraViewport* previewViewport = nullptr);
		bool PreparePreviewCamera(
		    Scene&, const ModelAsset&, const std::vector<AssetVersionRef<MeshAsset>>&, const CameraViewport&);
		bool UsePreviewScene(const CameraViewport&); /* 先装配独立候选场景，提交成功后才切换显示。 */
		void DrawImage(const ImageAsset&);

		JobSystem& m_Jobs;
		AssetManager& m_Assets;
		SceneManager& m_Scenes;
		JobHandle m_Job;
		std::shared_ptr<TaskState> m_Task;
		std::shared_ptr<const Tools::AssetBuildRequest> m_Recipe;
		Tools::AssetBuildResult m_Result;
		std::string m_Error, m_Phase;
		std::vector<AssetId> m_LoadOrder;
		AssetId m_PendingLoad;
		std::set<AssetId> m_ProjectAssets;
		std::map<AssetId, size_t> m_RecordIndices; /* 当前结果的索引，仅随成功安装的目录替换。 */
		size_t m_LoadCursor = 0;
		FileWatcher m_Watcher;
		bool m_WatchEnabled = false, m_ReimportPending = false;
		std::string m_WatchError;
		bool m_Loading = false;
		bool m_AssetsReady = false;
		bool m_Cancelled = false;
		bool m_InstantiateRequested = false;
		bool m_PreviewRequested = false;
		bool m_ReturnScene = false;
		AssetId m_Selected;
		AssetId m_OpenAsset;
		SceneHandle m_PreviewScene, m_PreviousScene;
		std::vector<std::pair<SceneHandle, EntityReference>> m_CreatedRoots;
	};
}
