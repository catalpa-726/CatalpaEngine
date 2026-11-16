#pragma once
#include "Panels/PanelText.h"
#include "Core/IO/FileWatcher.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "AssetPipeline/ProjectSourceImport.h"
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
	class SceneDocument;
	class BuildPanel final {
	public:
		BuildPanel(JobSystem&, AssetManager&, SceneManager&);
		~BuildPanel();
		BuildPanel(const BuildPanel&) = delete;
		BuildPanel& operator=(const BuildPanel&) = delete;
		/**
		 * @brief 按项目和实际源文件复用导入草稿；首次访问时恢复此来源上次成功构建的配方。
		 * @param[in] request 提供作者项目、源路径和构建环境。
		 * @param[out] error 无法解析源文件或验证已保存的配方时返回原因。
		 * @return 仅供主线程编辑的设置指针，在关闭项目前有效；读取失败返回空指针。
		 */
		Tools::AssetImportSettings* ImportSettings(const Tools::AssetBuildRequest& request, std::string& error);
		/**
		 * @brief 提交指定的资源构建请求。
		 *
		 * @param[in] request 描述本次请求的 Tools::AssetBuildRequest。
		 * @param[in] importDirectory 需要复制外部源时使用的项目目标目录。
		 * @param[in] approved 用户确认过的依赖清单，可为空。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(Tools::AssetBuildRequest request, std::string& error,
		    const std::filesystem::path& importDirectory = {},
		    std::shared_ptr<const Tools::ProjectImportReview> approved = {});
		/**
		 * @brief 扫描待导入源和它的依赖，完成后由主线程读取结果。
		 * @param[in] request 本次源的项目和导入配置。
		 * @param[in] directory 项目内的目标目录。
		 * @param[out] result 后台任务完成时发布的确认清单。
		 * @param[out] job 供调用方轮询和取消的任务句柄。
		 * @param[out] error 无法排队时的原因。
		 * @return 已成功交给后台线程时为 true。
		 */
		bool ReviewImport(Tools::AssetBuildRequest request, const std::filesystem::path& directory,
		    std::shared_ptr<Tools::ProjectImportReview> result, JobHandle& job, std::string& error);
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
		/** @brief 返回当前导入或加载进度；负值表示总工作量尚未确定。 */
		float Progress() const;
		bool TakePlacementRequest() noexcept { return std::exchange(m_InstantiateRequested, false); }
		bool Ready() const noexcept { return !Busy() && !m_Cancelled && m_Error.empty() && m_AssetsReady; }
		uint64_t Generation() const noexcept { return m_Generation; }
		const std::string& Diagnostic() const noexcept { return m_Error; }
		/**
		 * @brief 将选中模型准备为单个可撤销的场景子树。
		 * @param[in,out] doc 目标作者文档。
		 * @param[in] position 模型根的世界坐标，单位为米。
		 * @param[out] created 成功后选中的实体根。
		 * @param[out] error 构建依赖不匹配或提交失败的原因。
		 * @param[in] modelId 拖放请求指定的模型；未指定时沿用手动操作的当前选中模型。
		 * @return 成功向指定文档提交完整实例时为 true。
		 */
		bool PlaceModel(SceneDocument& doc, Math::Vec3 position, PersistentEntityId& created, std::string& error,
		    AssetId modelId = {});
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
		bool InstantiateModel(AssetId modelId, Scene&, std::shared_ptr<ModelInstantiationResult>&,
		    const CameraViewport* previewViewport = nullptr, EntityId parent = {});
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
		std::map<std::pair<std::filesystem::path, std::filesystem::path>, Tools::AssetImportSettings> m_ImportSettings;
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
		uint64_t m_Generation = 0;
	};
}
