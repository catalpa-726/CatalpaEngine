#pragma once
#include "Panels/PanelText.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetDependencyGraph.h"
#include "Scene/SceneManager.h"
#include "Scene/ModelInstantiation.h"
#include "Camera/CameraLens.h"
#include "Geometry/Bounds.h"

namespace Catalpa::Editor {
    // 以主窗口的像素宽高为模型包围盒计算预览相机和裁剪面，失败时保留输出。
    bool FitModelPreviewCamera(const Aabb& bounds, const CameraViewport& viewport, CameraLens& lens,
                               Math::TransformTRS& placement, std::string& error);

    // 工作区拥有任务；关闭窗口只隐藏。工作线程仅持值请求/共享结果，不捕获this。
    class BuildPanel final {
      public:
        BuildPanel(JobSystem&, AssetManager&, SceneManager&);
        ~BuildPanel();
        BuildPanel(const BuildPanel&) = delete;
        BuildPanel& operator=(const BuildPanel&) = delete;
        bool Start(Tools::AssetBuildRequest, std::string& error);
        void Update(const CameraViewport& viewport);
        void Cancel();
        void CloseProject();
        void Draw(bool& visible, const PanelText&);
        bool Busy() const noexcept;

        const Tools::AssetBuildResult& Result() const {
            return m_Result;
        }

      private:
        struct TaskState {
            std::mutex mutex;
            std::string phase;
            size_t done = 0, total = 1;
            Tools::AssetBuildResult result;
        };

        void Fail(std::string);
        void ReleaseProjectCache(); // 释放本面板请求的CPU缓存，固定场景快照仍保活自己的版本。
        void FinishBuild();
        void UpdateLoad();
        bool InstantiateSelectedModel(Scene&, std::shared_ptr<ModelInstantiationResult>&,
                                      const CameraViewport* previewViewport = nullptr);
        bool PreparePreviewCamera(Scene&, const ModelAsset&, const std::vector<AssetVersionRef<MeshAsset>>&,
                                  const CameraViewport&);
        bool UsePreviewScene(const CameraViewport&); // 先装配独立候选场景，提交成功后才切换显示。
        void DrawImage(const ImageAsset&);

        JobSystem& m_Jobs;
        AssetManager& m_Assets;
        SceneManager& m_Scenes;
        JobHandle m_Job;
        std::shared_ptr<TaskState> m_Task;
        Tools::AssetBuildResult m_Result;
        std::string m_Error, m_Phase;
        std::vector<AssetId> m_LoadOrder;
        AssetId m_PendingLoad;
        std::set<AssetId> m_ProjectAssets;
        std::map<AssetId, size_t> m_RecordIndices; // 当前结果的索引，仅随成功安装的目录替换。
        size_t m_LoadCursor = 0;
        bool m_Loading = false;
        bool m_AssetsReady = false;
        bool m_Cancelled = false;
        bool m_InstantiateRequested = false;
        bool m_PreviewRequested = false;
        bool m_ReturnScene = false;
        AssetId m_Selected;
        SceneHandle m_PreviewScene, m_PreviousScene;
        std::vector<std::pair<SceneHandle, EntityReference>> m_CreatedRoots;
    };
}
