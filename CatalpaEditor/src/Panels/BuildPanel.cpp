#include "catpch.h"
#include "Panels/BuildPanel.h"
#include "AssetPipeline/AssetCooker.h"
#include "Camera/CameraProjection.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Geometry/Bounds.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Log/Log.h"
#include "imgui.h"

namespace Catalpa::Editor {
    BuildPanel::BuildPanel(JobSystem& jobs, AssetManager& assets, SceneManager& scenes)
        : m_Jobs(jobs), m_Assets(assets), m_Scenes(scenes) {}

    BuildPanel::~BuildPanel() {
        CloseProject();
    }

    bool BuildPanel::Busy() const noexcept {
        return bool(m_Task) || m_Loading;
    }

    void BuildPanel::Fail(std::string error) {
        m_Error = std::move(error);
        m_Phase = "Request failed";
        m_Loading = false;
        m_InstantiateRequested = false;
        CAT_ERROR("Asset pipeline: {0}", m_Error);
    }

    void BuildPanel::ReleaseProjectCache() {
        for (auto id : m_ProjectAssets)
            m_Assets.Unload(id);
        m_ProjectAssets.clear();
    }

    bool BuildPanel::Start(Tools::AssetBuildRequest request, std::string& error) {
        if (Busy()) {
            error = "An asset request is still active";
            return false;
        }
        auto task = std::make_shared<TaskState>();
        task->phase = "Queued";
        // 文档只保留已成功安装的源/设置；后台进度回调及取消令牌不可进入草稿。
        request.cancellation = {};
        request.progress = {};
        task->recipe = std::make_shared<const Tools::AssetBuildRequest>(request);
        request.progress = [task](std::string_view phase, size_t done, size_t total) {
            std::lock_guard lock(task->mutex);
            task->phase = phase;
            task->done = done;
            task->total = total;
        };
        JobHandle job;
        if (!m_Jobs.Submit(
                [task, request = std::move(request)](const CancellationToken& token) mutable {
                    request.cancellation = token;
                    auto result = Tools::AssetCooker::Build(std::move(request));
                    std::lock_guard lock(task->mutex);
                    task->result = std::move(result);
                },
                job, error))
            return false;
        // 烘焙进行中保留上一次可用的目录和选择；新目录安装成功后才释放旧缓存。
        m_Task = std::move(task);
        m_Job = std::move(job);
        m_Error.clear();
        m_InstantiateRequested = false;
        m_PreviewRequested = false;
        m_Cancelled = false;
        error.clear();
        return true;
    }

    void BuildPanel::Cancel() {
        m_Job.Cancel();
        m_Cancelled = true;
        if (m_Loading && m_PendingLoad.HasValue()) {
            const auto status = m_Assets.GetStatus(m_PendingLoad);
            if (status == AssetStatus::Loading || status == AssetStatus::WaitingForDependencies)
                m_Assets.Cancel(m_PendingLoad);
        }
        m_PendingLoad = {};
        m_Loading = false;
        m_InstantiateRequested = false;
        m_PreviewRequested = false;
        m_Phase = "Request cancelled";
    }

    void BuildPanel::CloseProject() {
        Cancel();
        std::string error;
        if (m_Job.HasValue() && !m_Jobs.Wait(m_Job, error))
            CAT_ERROR("Asset build wait: {0}", error);
        m_Job = {};
        m_Task.reset();
        m_Result = {};
        m_Recipe.reset();
        m_OpenAsset = {};
        m_RecordIndices.clear();
        m_LoadOrder.clear();
        m_PendingLoad = {};
        m_Selected = {};
        m_Phase.clear();
        m_Error.clear();
        m_AssetsReady = false;
        m_ReturnScene = false;
        std::vector<SceneHandle> changed;
        for (const auto& [handle, root] : m_CreatedRoots)
            if (auto* scene = m_Scenes.GetScene(handle)) {
                const auto entity = scene->Resolve(root);
                if (entity != EntityId{}) {
                    const auto status = scene->QueueDestroySubtree(entity);
                    if (status.IsSuccess()) {
                        if (std::find(changed.begin(), changed.end(), handle) == changed.end())
                            changed.push_back(handle);
                    } else
                        CAT_ERROR("Model cleanup: {0}", status.message);
                }
            }
        for (auto handle : changed)
            if (auto* scene = m_Scenes.GetScene(handle)) {
                const auto status = scene->CommitPending();
                if (!status.IsSuccess())
                    CAT_ERROR("Model cleanup commit: {0}", status.message);
            }
        m_CreatedRoots.clear();
        if (m_PreviewScene.HasValue()) {
            const bool restore = m_Scenes.GetActiveHandle() == m_PreviewScene;
            const auto status = m_Scenes.Unload(m_PreviewScene);
            if (!status.IsSuccess())
                CAT_ERROR("Preview scene close: {0}", status.message);
            if (restore && m_Scenes.GetScene(m_PreviousScene)) {
                const auto active = m_Scenes.SetActive(m_PreviousScene);
                if (!active.IsSuccess())
                    CAT_ERROR("Restore scene: {0}", active.message);
            }
            m_PreviewScene = {};
            m_PreviousScene = {};
        }
        ReleaseProjectCache();
    }

    void BuildPanel::FinishBuild() {
        const auto taskError = m_Job.GetError();
        auto recipe = m_Task->recipe;
        Tools::AssetBuildResult next;
        {
            std::lock_guard lock(m_Task->mutex);
            next = std::move(m_Task->result);
        }
        m_Task.reset();
        m_Job = {};

        for (const auto& diagnostic : next.diagnostics) {
            if (diagnostic.level == Tools::AssetDiagnosticLevel::Error)
                CAT_ERROR("{} [{}]: {}", diagnostic.source, diagnostic.element, diagnostic.message);
            else if (diagnostic.level == Tools::AssetDiagnosticLevel::Warning)
                CAT_WARN("{} [{}]: {}", diagnostic.source, diagnostic.element, diagnostic.message);
            else
                CAT_INFO("{} [{}]: {}", diagnostic.source, diagnostic.element, diagnostic.message);
        }
        if (!taskError.empty()) {
            Fail(taskError);
            return;
        }
        if (m_Cancelled) {
            m_Phase = "Request cancelled; cooked results were not loaded";
            return;
        }
        if (!next.success) {
            Fail(next.error.empty() ? "Asset job failed without a result" : next.error);
            return;
        }

        AssetRegistry registry;
        AssetDependencyGraph graph;
        std::string error;
        if (!registry.Replace(next.publishedCatalog, error) || !graph.Build(next.records, error) ||
            !m_Assets.InstallCookedCatalog(next.cookedRoot, registry, error, true)) {
            Fail(error);
            return;
        }

        // Runtime 安装整份已发布目录，本面板只请求本次源资产及它的依赖。
        m_Result = std::move(next);
        m_Recipe = std::move(recipe);
        m_RecordIndices.clear();
        for (size_t i = 0; i < m_Result.records.size(); ++i)
            m_RecordIndices.emplace(m_Result.records[i].id, i);
        // 重复构建可复用同版本缓存。这里只回收由本面板加载且新源已不再使用的资产。
        for (auto it = m_ProjectAssets.begin(); it != m_ProjectAssets.end();) {
            if (m_RecordIndices.contains(*it)) {
                ++it;
                continue;
            }
            m_Assets.Unload(*it);
            it = m_ProjectAssets.erase(it);
        }
        m_PendingLoad = {};
        m_LoadOrder = graph.Order();
        m_LoadCursor = 0;
        m_Loading = true;
        m_AssetsReady = false;
        m_Selected = m_Result.primary;
        m_Phase = "Loading cooked versions";
    }

    void BuildPanel::UpdateLoad() {
        // 每帧按拓扑顺序准入一个资产，避免大模型一次占满待处理队列或解码预算。
        if (m_LoadCursor < m_LoadOrder.size()) {
            const auto id = m_LoadOrder[m_LoadCursor];
            const auto status = m_Assets.GetStatus(id);
            if (status == AssetStatus::Ready) {
                m_PendingLoad = {};
                ++m_LoadCursor;
            } else if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
                Fail(m_Assets.GetDiagnostic(id));
                return;
            } else if (status == AssetStatus::Unloaded) {
                std::string error;
                if (!m_Assets.Request(id, error)) {
                    Fail(error);
                    return;
                }
                // 引擎共享着色器继续归 Runtime 管理；面板只取消和释放自己的项目请求。
                if (m_Result.records[m_RecordIndices.at(id)].type.ToString() != "shader") {
                    m_PendingLoad = id;
                    m_ProjectAssets.insert(id);
                }
            }
        }
        if (m_LoadCursor == m_LoadOrder.size()) {
            m_Loading = false;
            m_AssetsReady = true;
            m_Phase = m_Result.cacheHit ? "Cache hit; cooked assets ready" : "Cooked assets ready";
        }
    }

    void BuildPanel::Update(const CameraViewport& viewport) {
        if (std::exchange(m_ReturnScene, false) && m_Scenes.GetScene(m_PreviousScene)) {
            const auto status = m_Scenes.SetActive(m_PreviousScene);
            if (!status.IsSuccess())
                Fail(status.message);
        }
        if (m_Task && m_Jobs.IsComplete(m_Job))
            FinishBuild();
        if (m_Loading)
            UpdateLoad();

        if (Busy() || !m_AssetsReady)
            return;
        if (std::exchange(m_PreviewRequested, false)) {
            if (!UsePreviewScene(viewport))
                Fail(m_Error);
        } else if (std::exchange(m_InstantiateRequested, false)) {
            const auto handle = m_Scenes.GetActiveHandle();
            std::shared_ptr<ModelInstantiationResult> instance;
            m_Error.clear();
            const auto status = m_Scenes.WithActive([&](Scene& scene) -> WorldStatus {
                if (!InstantiateSelectedModel(scene, instance))
                    return {WorldError::CommandFailed, "Cannot instantiate model"};
                return {};
            });
            if (!status.IsSuccess())
                Fail(m_Error.empty() ? status.message : m_Error);
            if (instance && instance->success) {
                for (auto root : instance->roots)
                    m_CreatedRoots.emplace_back(handle, root);
                m_Phase = "Model instantiated at the scene safe point";
            }
        }
    }

    bool BuildPanel::UsePreviewScene(const CameraViewport& viewport) {
        // 候选场景的相机、模型或材质出错时只卸载候选，旧预览及当前活动场景仍可用。
        SceneHandle candidate;
        const auto created = m_Scenes.CreateScene("Imported model preview", candidate);
        if (!created.IsSuccess()) {
            m_Error = created.message;
            return false;
        }
        const auto discard = [&]() {
            const auto status = m_Scenes.Unload(candidate);
            if (!status.IsSuccess())
                CAT_ERROR("Cannot release preview candidate: {}", status.message);
            return false;
        };
        try {
            auto* scene = m_Scenes.GetScene(candidate);
            if (!scene) {
                m_Error = "Preview scene is unavailable";
                return discard();
            }
            std::shared_ptr<ModelInstantiationResult> result;
            if (!InstantiateSelectedModel(*scene, result, &viewport))
                return discard();

            // 复用普通帧提取的像素视口和相机校验，候选无效时留在原场景。
            RenderFramePacket frame;
            auto status = TransformSystem::Update(*scene);
            if (status.IsSuccess())
                status = RenderExtractSystem::Extract(*scene, 1, viewport.width, viewport.height, frame);
            if (!status.IsSuccess() || frame.views.size() != 1) {
                m_Error = status.IsSuccess() ? "Preview requires one active camera" : status.message;
                return discard();
            }
            const auto valid = ValidateRenderView(frame.views.front());
            if (!valid.IsSuccess()) {
                m_Error = valid.message;
                return discard();
            }
        } catch (const std::exception& exception) {
            m_Error = exception.what();
            return discard();
        }

        const auto previous = m_Scenes.GetActiveHandle();
        const auto active = m_Scenes.SetActive(candidate);
        if (!active.IsSuccess()) {
            m_Error = active.message;
            return discard();
        }
        if (previous != m_PreviewScene)
            m_PreviousScene = previous;
        const auto replaced = m_PreviewScene;
        m_PreviewScene = candidate;
        if (replaced.HasValue()) {
            const auto status = m_Scenes.Unload(replaced);
            if (!status.IsSuccess()) {
                m_Error = status.message;
                return false;
            }
            std::erase_if(m_CreatedRoots, [&](const auto& owned) { return owned.first == replaced; });
        }
        m_Error.clear();
        m_Phase = "Model preview ready";
        return true;
    }

    bool FitModelPreviewCamera(const Aabb& bounds, const CameraViewport& viewport, CameraLens& lens,
                               Math::TransformTRS& placement, std::string& error) {
        if (viewport.width == 0 || viewport.height == 0) {
            error = "Preview requires a nonempty framebuffer";
            return false;
        }
        BoundingSphere sphere;
        const auto bounded = ComputeBoundingSphere(bounds, sphere);
        if (!bounded.IsSuccess() || sphere.empty) {
            error = "Preview model has no finite bounds";
            return false;
        }
        CameraLens candidate;
        // 球体与视锥相切时 sin(halfFov) = radius / distance；按较窄的视场留出边距。
        const double radius = std::max(double(sphere.radius), 0.001);
        const double halfVertical = candidate.verticalFovRadians * 0.5;
        const double aspect = double(viewport.width) / viewport.height;
        const double halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
        const double fitDistance = radius / std::sin(std::min(halfVertical, halfHorizontal));
        const double cameraZ = double(sphere.center.z) + fitDistance * 2.5;
        if (!std::isfinite(cameraZ) || std::abs(cameraZ) > std::numeric_limits<float>::max()) {
            error = "Preview camera position is outside float range";
            return false;
        }
        Math::TransformTRS pose;
        pose.translation = {sphere.center.x, sphere.center.y, static_cast<float>(cameraZ)};
        const double depth = double(pose.translation.z) - sphere.center.z;
        const double tightNear = depth - 1.5 * radius, tightFar = depth + 1.5 * radius;
        if (depth < fitDistance || tightNear <= 0.0 || tightFar > std::numeric_limits<float>::max()) {
            error = "Preview bounds and camera position cannot be represented at this scale";
            return false;
        }

        // 给前后移动留空间；极窄视口需要很远的初始机位，限制远近面比以保留深度精度。
        const double farPlane = std::min(depth + 16.0 * radius, double(std::numeric_limits<float>::max()));
        candidate.nearPlane = static_cast<float>(std::max(radius * 0.01, farPlane / 10000.0));
        candidate.farPlane = static_cast<float>(farPlane);
        Math::Mat4 world;
        if (const auto status = Math::ComposeTRS(pose, world); !status.IsSuccess()) {
            error = status.message;
            return false;
        }
        CameraMatrices matrices;
        if (!BuildCameraMatrices(world, candidate, viewport, matrices).IsSuccess()) {
            // 远离世界原点时，较小近面可能超出位置精度预算。此时保留原有的包围球取景。
            candidate.nearPlane = static_cast<float>(tightNear);
            candidate.farPlane = static_cast<float>(tightFar);
            if (const auto status = BuildCameraMatrices(world, candidate, viewport, matrices); !status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }

        lens = candidate;
        placement = pose;
        error.clear();
        return true;
    }

    bool BuildPanel::PreparePreviewCamera(Scene& scene, const ModelAsset& source,
                                          const std::vector<AssetVersionRef<MeshAsset>>& meshes,
                                          const CameraViewport& viewport) {
        Aabb bounds;
        CameraComponent camera;
        Math::TransformTRS placement;
        if (!ComputeModelBounds(source, meshes, bounds, m_Error) ||
            !FitModelPreviewCamera(bounds, viewport, camera.lens, placement, m_Error))
            return false;
        PersistentEntityId cameraId;
        if (!GeneratePersistentEntityId(cameraId, m_Error))
            return false;
        const auto queued = scene.QueueCreate(cameraId, "Preview camera", placement);
        const auto created = queued.IsSuccess() ? scene.CommitPending() : queued;
        if (!created.IsSuccess()) {
            m_Error = created.message;
            return false;
        }
        const auto configured = scene.QueueSetCamera(scene.Resolve(cameraId), camera);
        if (!configured.IsSuccess()) {
            m_Error = configured.message;
            return false;
        }
        PersistentEntityId lightId;
        if (!GeneratePersistentEntityId(lightId,m_Error)) return false;
        Math::TransformTRS lightPose;
        lightPose.rotation = Math::Quat(Math::Vec3(-.55f,.65f,0));
        auto status = scene.QueueCreate(lightId,"Preview light",lightPose);
        if (status.IsSuccess()) status = scene.CommitPending();
        if (status.IsSuccess()) status = scene.QueueSetLight(scene.Resolve(lightId),LightComponent{});
        if (!status.IsSuccess()) { m_Error = status.message; return false; }
        CAT_INFO("Preview bounds in metres: min=({0},{1},{2}), max=({3},{4},{5}); camera framing only.",
                 bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y,
                 bounds.maximum.z);
        return true;
    }

    bool BuildPanel::InstantiateSelectedModel(Scene& scene, std::shared_ptr<ModelInstantiationResult>& result,
                                              const CameraViewport* previewViewport) {
        // 查询时同时核对 ID 和内容版本。回退或上一次构建的版本均不能进入这次场景。
        const auto acquireExact = [&](AssetId id, auto& value) {
            bool fallback = false;
            if (!m_Assets.Acquire(id, value, fallback, m_Error))
                return false;
            const auto found = m_RecordIndices.find(id);
            if (fallback || value.GetId() != id || found == m_RecordIndices.end() ||
                m_Result.records[found->second].version != value.GetVersion()) {
                m_Error = "Expected exact cooked version of " + id.ToString();
                return false;
            }
            return true;
        };

        AssetVersionRef<ModelAsset> model;
        if (!acquireExact(m_Selected, model))
            return false;

        std::vector<AssetVersionRef<MeshAsset>> meshes;
        std::vector<AssetVersionRef<MaterialInstance>> materials;
        std::set<AssetId> meshIds, materialIds;
        for (const auto& instance : model.Get()->meshes) {
            if (meshIds.insert(instance.mesh.id).second) {
                AssetVersionRef<MeshAsset> mesh;
                if (!acquireExact(instance.mesh.id, mesh))
                    return false;
                meshes.push_back(std::move(mesh));
            }
            for (const auto material : instance.materials) {
                if (!materialIds.insert(material.id).second)
                    continue;
                AssetVersionRef<MaterialInstance> value;
                if (!acquireExact(material.id, value))
                    return false;
                materials.push_back(std::move(value));

            }
        }
        if (previewViewport && !PreparePreviewCamera(scene, *model.Get(), meshes, *previewViewport))
            return false;
        // 只在工作区的主线程更新阶段提交，通过 ticket 同时检查整队列和模型事务的结果。
        if (!ModelInstantiation::Queue(scene, std::move(model), std::move(meshes), std::move(materials),
                                       result, m_Error))
            return false;
        const auto committed = scene.CommitPending();
        if (!committed.IsSuccess()) {
            result->cancelRequested = true;
            m_Error = committed.message;
            return false;
        }
        if (!result->complete || !result->success) {
            m_Error = result->error.empty() ? "Model transaction did not complete" : result->error;
            return false;
        }
        m_Error.clear();
        return true;
    }

    void BuildPanel::DrawImage(const ImageAsset& value) {
        const auto& image = value.image;
        ImGui::Text("%u x %u, %s, semantic=%u", image.width, image.height,
                    image.colorSpace == ImageColorSpace::SRGB ? "sRGB" : "Linear", unsigned(image.semantic));
        if (!image.width || !image.height || image.subresources.empty() ||
            (image.format != PixelFormat::RGBA8_UNorm && image.format != PixelFormat::RGBA8_SRGB))
            return;
        // CPU像素格预览不建立第二个GPU纹理所有者；仅显示采样点，资产字节不变。
        const auto& sub = image.subresources.front();
        const unsigned countX = std::min(image.width, 64u), countY = std::min(image.height, 64u);
        const float scale = std::min(256.0f / float(std::max(image.width, image.height)),
                                     std::max(1.0f, ImGui::GetContentRegionAvail().x) / float(image.width)),
                    width = scale * image.width, height = scale * image.height;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        for (unsigned y = 0; y < countY; ++y)
            for (unsigned x = 0; x < countX; ++x) {
                const size_t px = uint64_t(x) * image.width / countX,
                             py = uint64_t(y) * image.height / countY;
                const size_t offset = size_t(sub.offset + py * sub.rowPitch + px * 4);
                if (offset + 4 > image.pixels.size())
                    continue;
                const auto* color = image.pixels.data() + offset;
                draw->AddRectFilled(
                    {origin.x + width * x / countX, origin.y + height * y / countY},
                    {origin.x + width * (x + 1) / countX, origin.y + height * (y + 1) / countY},
                    IM_COL32(std::to_integer<int>(color[0]), std::to_integer<int>(color[1]),
                             std::to_integer<int>(color[2]), std::to_integer<int>(color[3])));
            }
        ImGui::Dummy({width, height});
    }

    void BuildPanel::Draw(bool& visible, const PanelText& text) {
        if (!visible)
            return;
        if (ImGui::Begin(text("panel.build", "Build", "Catalpa.Build").c_str(), &visible)) {
            if (m_Task) {
                std::lock_guard lock(m_Task->mutex);
                ImGui::TextUnformatted(m_Task->phase.c_str());
                ImGui::ProgressBar(m_Task->total ? float(m_Task->done) / float(m_Task->total) : 0);
            } else {
                ImGui::TextWrapped("%s", m_Phase.c_str());
                if (m_Loading)
                    ImGui::ProgressBar(float(m_LoadCursor) / float(m_LoadOrder.size()));
            }
            if (m_Scenes.GetActiveHandle() == m_PreviewScene && m_PreviewScene.HasValue() &&
                m_Scenes.GetScene(m_PreviousScene) &&
                ImGui::Button(text("build.return_scene", "Return to previous scene", "ReturnScene").c_str()))
                m_ReturnScene = true;
            if (Busy() && ImGui::Button(text("build.cancel", "Cancel request", "CancelBuild").c_str()))
                Cancel();
            if (!m_Error.empty())
                ImGui::TextWrapped("%s", m_Error.c_str());
            if (m_Result.success) {
                ImGui::TextWrapped("%s", Tools::AssetPathText(m_Result.report).c_str());
                for (const auto& diagnostic : m_Result.diagnostics)
                    ImGui::TextWrapped("%s: %s", diagnostic.element.c_str(), diagnostic.message.c_str());
                if (ImGui::BeginListBox("##CookedAssets", {0, 140})) {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(m_Result.records.size()));
                    while (clipper.Step())
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                            const auto& record = m_Result.records[size_t(i)];
                            ImGui::PushID(record.id.ToString().c_str());
                            const auto position = ImGui::GetCursorScreenPos();
                            if (ImGui::Selectable("##Asset", m_Selected == record.id))
                                m_Selected = record.id;
                            const auto label = record.type.ToString() + " " + record.id.ToString();
                            ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text),
                                                                label.c_str());
                            ImGui::PopID();
                        }
                    ImGui::EndListBox();
                }
                const auto found = m_RecordIndices.find(m_Selected);
                if (found != m_RecordIndices.end()) {
                    const auto* selected = &m_Result.records[found->second];
                    const bool environment = selected->type.ToString() == "environment";
                    if (selected->type.ToString() == "material_instance" || selected->type.ToString() == "image" || environment ||
                        selected->type.ToString() == "scene" || selected->type.ToString() == "prefab" ||
                        selected->type.ToString() == "script" || selected->type.ToString() == "behavior_graph" || selected->type.ToString()=="material_definition") {
                        ImGui::BeginDisabled(Busy() || !m_AssetsReady);
                        const auto action = environment ? text("build.apply_environment","Apply to selected entity","ApplyEnvironment")
                                                        : text("build.edit_asset","Open asset editor","EditAsset");
                        if (ImGui::Button(action.c_str()))
                            m_OpenAsset = selected->id;
                        ImGui::EndDisabled();
                    }
                    ImGui::Text("version: %llu", static_cast<unsigned long long>(selected->version));
                    if (selected->type.ToString() == "model") {
                        ImGui::BeginDisabled(Busy() || !m_AssetsReady);
                        if (ImGui::Button(
                                text("build.instantiate", "Instantiate selected model", "InstantiateModel")
                                    .c_str()))
                            m_InstantiateRequested = true;
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(Busy() || !m_AssetsReady);
                        if (ImGui::Button(
                                text("build.preview", "Preview in current window", "PreviewModel").c_str()))
                            m_PreviewRequested = true;
                        ImGui::EndDisabled();
                        AssetVersionRef<ModelAsset> model;
                        bool fallback = false;
                        std::string error;
                        if (m_Assets.Acquire(selected->id, model, fallback, error) && !fallback &&
                            model.GetVersion() == selected->version) {
                            ImGui::Text("nodes: %zu, mesh instances: %zu", model.Get()->nodes.size(),
                                        model.Get()->meshes.size());
                            if (ImGui::TreeNode(text("build.hierarchy", "Imported node hierarchy / local TRS",
                                                     "ImportedHierarchy")
                                                    .c_str())) {
                                ImGuiListClipper clipper;
                                clipper.Begin(int(model.Get()->nodes.size()));
                                while (clipper.Step())
                                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                                        const auto& node = model.Get()->nodes[size_t(i)];
                                        ImGui::Text("%d parent=%d %s  T=(%.4g,%.4g,%.4g) S=(%.4g,%.4g,%.4g)",
                                                    i, node.parent, node.name.c_str(),
                                                    node.local.translation.x, node.local.translation.y,
                                                    node.local.translation.z, node.local.scale.x,
                                                    node.local.scale.y, node.local.scale.z);
                                    }
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TextWrapped(
                            "%s",
                            text("build.preview_scope",
                                 "Preview uses the model's authored material slots.",
                                 nullptr)
                                .c_str());
                    } else if (selected->type.ToString() == "image") {
                        AssetVersionRef<ImageAsset> image;
                        bool fallback = false;
                        std::string error;
                        if (m_Assets.Acquire(selected->id, image, fallback, error) && !fallback &&
                            image.GetVersion() == selected->version) {
                            ImGui::TextWrapped("%s", text("build.image_scope",
                                                          "Sampled CPU image preview; original pixels and "
                                                          "color-space tags are unchanged.",
                                                          nullptr)
                                                         .c_str());
                            DrawImage(*image.Get());
                        }
                    }
                }
            }
        }
        ImGui::End();
    }
}
