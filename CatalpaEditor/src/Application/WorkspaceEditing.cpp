#include "Application/WorkspaceEditing.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Core/Application.h"
#include "Core/Window.h"
#include "Commands/AssetCommands.h"
#include "Commands/SceneCommands.h"
#include "Panels/BuildPanel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t SceneSurface = 0x43415453434e4501ULL;

        void DrawImportSettings(AssetDocument& doc, BuildPanel* build, const PanelText& text,
                                std::string& error) {
            if (ImGui::CollapsingHeader(
                    text("edit.cook_diagnostics", "Cook diagnostics", "CookDiagnostics").c_str())) {
                ImGui::TextWrapped("%s", Tools::AssetPathText(doc.Report()).c_str());
                for (const auto& diagnostic : doc.Diagnostics())
                    ImGui::TextWrapped("%s: %s", diagnostic.element.c_str(), diagnostic.message.c_str());
            }
            if (!build || !doc.Recipe() ||
                !ImGui::CollapsingHeader(
                    text("edit.import_settings", "Source import settings", "ImportSettings").c_str()))
                return;
            const auto& recipe = *doc.Recipe();
            const bool authorSource = bool(doc.Source());
            const auto& source = authorSource ? doc.AuthorPath() : recipe.source;
            ImGui::TextWrapped("%s", Tools::AssetPathText(source).c_str());
            const auto& settings = recipe.settings;
            double scale = settings.unitScale;
            bool normals = settings.generateNormals, tangents = settings.generateTangents;
            bool changed =
                ImGui::InputDouble(text("content.scale", "Additional unit scale", "DraftScale").c_str(),
                                   &scale, 0, 0, "%.5g", ImGuiInputTextFlags_EnterReturnsTrue);
            changed |= ImGui::Checkbox(
                text("edit.normals", "Generate missing normals", "DraftNormals").c_str(), &normals);
            changed |= ImGui::Checkbox(
                text("edit.tangents", "Generate missing tangents", "DraftTangents").c_str(), &tangents);
            if (changed)
                ChangeImportSettings(doc, scale, normals, tangents, error);
            const bool needsSave = authorSource && (source.empty() || doc.History().Dirty());
            if (needsSave) ImGui::TextUnformatted("Save the author document before rebuilding.");
            ImGui::BeginDisabled(build->Busy() || needsSave);
            if (ImGui::Button(text("edit.reimport", "Rebuild from this draft", "Reimport").c_str())) {
                auto request = *doc.Recipe();
                // 作者文件另存后使用新路径；模型导出的材质仍从原模型重新导入。
                if (authorSource) request.source = source;
                build->Start(request, error);
            }
            ImGui::EndDisabled();
        }

        // 包围盒来自当前选中实例的实际 CPU 几何与已发布世界变换，不在编辑器缓存 GPU 缓冲。
        void DrawSelectedBounds(Application& app, SceneDocument& doc, const EditorSelection& selection,
                                std::string& error) {
            auto* scene = doc.Resolve();
            const auto entity = selection.Resolve(doc);
            if (!scene || !entity.HasValue())
                return;
            MeshRendererComponent mesh;
            TransformComponent transform;
            if (!scene->GetWorld().Get(entity, mesh).IsSuccess() || !mesh.visible ||
                !scene->GetTransform(entity, transform).IsSuccess() || !transform.worldValid)
                return;
            const auto geometry = mesh.meshVersion.HasValue() ? mesh.meshVersion.Get()->mesh : mesh.mesh;
            if (!geometry)
                return;
            Aabb bounds;
            if (TransformBounds(geometry->GetData().localBounds, transform.worldMatrix, bounds).IsSuccess() &&
                !bounds.empty) {
                std::string reason;
                if (!app.AddDebugBounds(bounds, {0.15f, 0.78f, 0.49f, 1.f}, reason, SceneSurface))
                    error = std::move(reason);
            }
        }
    }

    WorkspaceEditing::WorkspaceEditing(Application& app) : m_App(app) {
        m_HostScene=m_App.GetScenes().GetActiveHandle();
        m_App.SetEditorViewportActive(true);
    }

    WorkspaceEditing::~WorkspaceEditing() {
        std::string ignored;
        (void)Reset(ignored);
        m_App.SetEditorViewportActive(false);
    }

    bool WorkspaceEditing::Reset(std::string& error) {
        if (!StopPlay()) { error = m_Error; return false; }
        m_Picking.Cancel(m_App);
        m_Gizmo.Cancel(m_Error);
        m_Selection.Clear();
        m_App.CloseEditorPreview(SceneSurface);
        m_AssetEditors.Close(m_App);
        m_PendingScene.reset();
        m_PendingAuthor.reset();
        for (auto* doc : m_Documents.All()) if (!doc->History().Dirty()) RemoveRecovery(doc->Id());
        m_RestoredRecovery.clear();
        for(auto handle:m_AuthorScenes) if(m_App.GetScenes().GetScene(handle)) {
            const auto status=m_App.GetScenes().Unload(handle);
            if(!status.IsSuccess()) {m_Error=status.message;error=m_Error;return false;}
        }
        m_AuthorScenes.clear();
        if(m_App.GetScenes().GetScene(m_HostScene)) (void)m_App.GetScenes().SetActive(m_HostScene);
        m_Documents.Clear();
        m_OpenAsset = {};
        m_LastScene = {};
        m_Error.clear();
        m_AskDiscard = false;
        m_RightLook = false;
        m_Project.reset();
        m_Recovery.clear();
        m_RecoveryRoot.clear();
        error.clear();
        return true;
    }

    bool WorkspaceEditing::HasDirtyDocuments() const {
        return m_Documents.HasDirtyDocuments();
    }

    SceneDocument* WorkspaceEditing::ActiveScene() {
        if (auto* play = dynamic_cast<SceneDocument*>(m_Documents.Find(m_PlayDocument))) {
            if (play->Resolve() && m_App.GetScenes().GetScene(play->PlayHandle()) &&
                m_App.GetScenes().GetActiveHandle() == play->PlayHandle()) return play;
            if (!StopPlay()) return nullptr;
        }
        auto& scenes = m_App.GetScenes();
        const auto handle = scenes.GetActiveHandle();
        if (!handle.HasValue())
            return nullptr;
        return m_Documents.SceneFor(scenes, handle, m_Error);
    }

    void WorkspaceEditing::Menu(const PanelText& text) {
        DocumentMenu();
        if (ImGui::BeginMenu(text("menu.edit", "Edit", nullptr).c_str())) {
            auto* doc = m_Documents.Active();
            if (ImGui::MenuItem(text("edit.undo", "Undo", nullptr).c_str(), nullptr, false,
                                doc && !m_PlayDocument.HasValue() && doc->History().CanUndo()))
                doc->History().Undo(*doc, m_Error);
            if (ImGui::MenuItem(text("edit.redo", "Redo", nullptr).c_str(), nullptr, false,
                                doc && !m_PlayDocument.HasValue() && doc->History().CanRedo()))
                doc->History().Redo(*doc, m_Error);
            ImGui::EndMenu();
        }
    }

    void WorkspaceEditing::SceneTree(const PanelText& text) {
        if (auto* doc = ActiveScene()) {
            ImGui::BeginDisabled(doc->Playing());
            const bool edited = m_Hierarchy.Draw(*doc, m_Selection, text, m_Error);
            // 层级中的点击/创建/删除比上一帧的像素读回更新，迟到结果不可覆盖它。
            if (edited)
                m_Picking.Cancel(m_App);
            if (ImGui::IsWindowFocused() || edited)
                m_Documents.Activate(doc->Id());
            ImGui::EndDisabled();
        }
    }

    bool WorkspaceEditing::Inspect(const PanelText& text) {
        auto* doc = ActiveScene();
        if (doc && m_Selection.Resolve(*doc).HasValue()) {
            if (doc->Playing()) { ImGui::TextUnformatted("Stop Play to edit author components."); return true; }
            if (ImGui::IsWindowFocused())
                m_Documents.Activate(doc->Id());
            const bool inspected=m_Inspector.Draw(*doc, m_Selection, text, m_Error);
            AuthorComponentControls(*doc);
            return inspected;
        }
        return false;
    }

    void WorkspaceEditing::ActivateAsset(AssetDocument& document) {
        m_Picking.Cancel(m_App);
        m_Gizmo.Cancel(m_Error);
        m_Selection.Clear();
        m_Documents.Activate(document.Id());
    }

    void WorkspaceEditing::OpenAsset(AssetId id, const BuildPanel& build) {
        if (m_PlayDocument.HasValue()) {
            m_Error = "Stop Play before opening an author asset";
            return;
        }
        CancelPendingOpen();
        auto& assets = m_App.GetAssets();
        bool fallback = false;
        AssetVersionRef<EnvironmentAsset> environment;
        const auto& records = build.Result().records;
        const auto found =
            std::find_if(records.begin(), records.end(), [id](const auto& entry) { return entry.id == id; });
        if (found == records.end()) {
            m_Error = "Selected asset is outside the installed cook result";
            return;
        }
        const auto type = found->type.ToString();
        if (build.Recipe()) {
            const auto ext = Tools::AssetSourceExtension(build.Recipe()->source);
            if (type == "scene") {
                const auto path = std::filesystem::canonical(build.Recipe()->source);
                if (m_Documents.FindByPath(path, m_Error)) {
                    (void)OpenAuthor(path);
                    return;
                }
                if (!m_Error.empty()) return;
                AssetVersionRef<SceneAsset> fixed;
                if (!assets.Acquire(id, fixed, fallback, m_Error) || fallback) return;
                (void)InstallAuthorScene(*fixed.Get(), path);
                return;
            }
            if (type == "prefab" || type == "script" || type == "behavior_graph" || type=="material_definition" || ext == ".matgraph") {
                if (OpenAuthor(build.Recipe()->source)) {
                    if (auto* doc = dynamic_cast<AssetDocument*>(m_Documents.Active()); doc && type!="scene") {
                        doc->SetSourceIdentity(ext == ".matgraph" ? build.Result().primary : id);
                        doc->AttachRecipe(build.Recipe());
                        doc->SetCookReport(build.Result());
                    }
                }
                return;
            }
        }
        if (type == "environment") {
            if (!assets.Acquire(id, environment, fallback, m_Error) || fallback) {
                m_Error = "The selected environment is not loaded";
                return;
            }
            auto* scene = ActiveScene();
            const auto entity = scene ? m_Selection.Resolve(*scene) : EntityId{};
            if (!scene || !entity.HasValue()) {
                m_Error = "Select a scene entity before assigning an environment";
                return;
            }
            EnvironmentComponent value;
            if (scene->Resolve()->GetWorld().Has<EnvironmentComponent>(entity))
                if (!scene->Resolve()->GetWorld().Get(entity, value).IsSuccess()) {
                    m_Error = "Environment entity is unavailable";
                    return;
                }
            value.environment = AssetHandle<EnvironmentAsset>{id};
            if (EditEnvironment(*scene, scene->Resolve()->MakeReference(entity).persistent, value, m_Error))
                m_Documents.Activate(scene->Id());
            return;
        }
        if (auto* document = m_AssetEditors.Open(id, type, assets, m_Documents, m_Error)) {
            ActivateAsset(*document);
            document->AttachRecipe(build.Recipe());
            document->SetCookReport(build.Result());
            m_OpenAsset = document->Id();
            m_AskDiscard = false;
            m_Error.clear();
        } else if (m_Error.empty())
            m_Error = "Select a loaded material instance or image";
    }

    EditorCameraInput WorkspaceEditing::ViewportInput(bool hovered, bool gizmoBusy) {
        EditorCameraInput input;
        const auto& io = ImGui::GetIO();
        if (!hovered || gizmoBusy || ImGui::IsAnyItemActive() || io.WantTextInput || io.AppFocusLost ||
            io.KeyCtrl || io.KeyAlt || io.KeySuper) {
            m_RightLook = false;
            return input;
        }

        // 滚轮跟随悬停的图像；其他面板已消费的滚动，以及 Shift 横向滚动，不驱动相机。
        // 键盘导航和右键转向仍要求视口获得焦点。
        if (io.MouseWheel != 0 && !io.KeyShift &&
            ImGui::GetCurrentContext()->WheelingWindowScrolledFrame != ImGui::GetFrameCount() &&
            ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetCurrentWindowRead()->ID))
            input.wheel = io.MouseWheel;

        const bool rightPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (rightPressed)
            ImGui::SetWindowFocus();
        if (!ImGui::IsWindowFocused()) {
            m_RightLook = false;
            return input;
        }
        m_App.AllowEditorViewportSystemKeys();
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
            return float(ImGui::IsKeyDown(positive)) - float(ImGui::IsKeyDown(negative));
        };
        input.seconds = io.DeltaTime;
        input.right = axis(ImGuiKey_D, ImGuiKey_A);
        input.forward = axis(ImGuiKey_W, ImGuiKey_S);
        input.up = axis(ImGuiKey_E, ImGuiKey_Q);
        if (rightPressed)
            m_RightLook = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_RightLook = false;
        if (m_RightLook && !rightPressed) {
            input.lookX = io.MouseDelta.x;
            input.lookY = io.MouseDelta.y;
        }
        return input;
    }

    void WorkspaceEditing::DrawViewport(SceneDocument* doc, const PanelText& text, uint32_t centerDockId) {
        // 仅迁移这个稳定窗口：旧版保存的浮动位置或侧边 DockId 不影响其他面板。
        if (centerDockId)
            ImGui::SetNextWindowDockID(centerDockId, ImGuiCond_Always);
        ImGuiWindowClass viewportClass;
        viewportClass.DockNodeFlagsOverrideSet =
            ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit |
            ImGuiDockNodeFlags_NoDockingOverMe | ImGuiDockNodeFlags_NoWindowMenuButton;
        ImGui::SetNextWindowClass(&viewportClass);
        const bool shown = ImGui::Begin(text("panel.viewport", "Scene Viewport", "Catalpa.Viewport").c_str(),
                                        nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoScrollWithMouse);
        if (!shown || !doc) {
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_RightLook = false;
            m_App.CloseEditorPreview(SceneSurface);
            if (shown)
                ImGui::TextDisabled("%s", text("viewport.no_scene", "No active scene.", nullptr).c_str());
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused())
            m_Documents.Activate(doc->Id());
        PlayToolbar(*doc);
        if (!doc->Playing()) m_Gizmo.Toolbar(text);
        if (ImGui::Button(text("viewport.reset_camera", "Reset view", "ResetEditorCamera").c_str())) {
            m_Gizmo.Cancel(m_Error);
            m_RightLook = false;
            m_Picking.Cancel(m_App);
            m_ResetCamera = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text("viewport.navigation",
                                         "Click the image and keep the pointer over it. WASD moves, Q/E descends or rises, "
                                         "hold right mouse to look. Scroll over the image to zoom.",
                                         nullptr)
                                        .c_str());
        ImGui::SameLine();
        double gpuMs = 0;
        const auto label = text("viewport.gpu_time", "GPU frame (ms)", nullptr);
        if (m_App.GetGpuFrameMilliseconds(gpuMs))
            ImGui::Text("%s: %.2f", label.c_str(), gpuMs);
        else
            ImGui::TextDisabled("%s: N/A", label.c_str());
        if (!doc->Playing()) DrawSelectedBounds(m_App, *doc, m_Selection, m_Error);

        const auto available = ImGui::GetContentRegionAvail(), origin = ImGui::GetCursorScreenPos();
        const auto density = ImGui::GetWindowViewport()->FramebufferScale;
        if (available.x < 1 || available.y < 1) {
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_RightLook = false;
            m_App.CloseEditorPreview(SceneSurface);
            ImGui::End();
            return;
        }
        const Extent2D extent{uint32_t(std::clamp(available.x * density.x, 1.f, 4096.f)),
                              uint32_t(std::clamp(available.y * density.y, 1.f, 4096.f))};
        RenderSurfaceSnapshot frame;
        const auto* renderedScene = m_App.GetScenes().GetScene(doc->Playing() ? doc->PlayHandle() : doc->Handle());
        const bool ready = renderedScene && (doc->Playing() || !m_ResetCamera) &&
                           m_App.GetEditorPreview(SceneSurface, frame) && frame.textureToken &&
                           frame.extent == extent && frame.view.independentCamera != doc->Playing() &&
                           frame.sceneIdentity == renderedScene->GetWorld().GetIdentity();
        if (!doc->Playing()) m_Picking.Update(m_App, doc, ready ? &frame : nullptr, m_Selection, m_Error);
        EditorCameraInput input;
        if (ready) {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(frame.textureToken)), available);
            const bool hovered = ImGui::IsItemHovered(),
                       clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            if (doc->Playing()) {
                if (clicked) ImGui::SetWindowFocus();
                if (hovered && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput)
                    m_App.AllowEditorPlayInput();
                (void)m_App.SetEditorPreview(doc->PlayHandle(), SceneSurface, extent, m_Error);
                ImGui::End();
                return;
            }
            const bool gizmo = m_Gizmo.Draw(*doc, m_Selection, frame.view, origin.x, origin.y, available.x,
                                            available.y, m_Error);
            input = ViewportInput(hovered, gizmo);
            if (clicked && !gizmo) {
                const auto mouse = ImGui::GetIO().MousePos;
                const auto x = uint32_t(std::clamp((mouse.x - origin.x) / available.x * extent.width, 0.f,
                                                   float(extent.width - 1)));
                const auto y = uint32_t(std::clamp((mouse.y - origin.y) / available.y * extent.height, 0.f,
                                                   float(extent.height - 1)));
                m_Picking.Request(m_App, *doc, frame, x, y, m_Error);
            }
        } else {
            m_RightLook = false;
            m_Gizmo.Cancel(m_Error);
            ImGui::TextDisabled("%s",
                                text("viewport.preparing", "Preparing scene image...", nullptr).c_str());
        }

        auto next = m_Camera;
        if (doc->Playing()) {
            (void)m_App.SetEditorPreview(doc->PlayHandle(), SceneSurface, extent, m_Error);
            ImGui::End();
            return;
        }
        std::string viewError;
        if (m_ResetCamera) {
            // 用真实输出尺寸筛选可作为导航起点的相机，坏的场景镜头不能锁住编辑视口。
            if (!next.Reset(doc->Resolve(), viewError, {extent.width, extent.height})) {
                m_Error = std::move(viewError);
                ImGui::End();
                return;
            }
            if (!viewError.empty()) m_Error = viewError;
        }
        if (next.Navigate(input, viewError) &&
            m_App.SetEditorCameraPreview(doc->Handle(), SceneSurface, extent, next.Pose(), next.Lens(),
                                         next.HistoryRevision(), viewError)) {
            m_Camera = next;
            m_ResetCamera = false;
        } else {
            m_RightLook = false;
            m_Error = std::move(viewError);
        }
        ImGui::End();
    }

    void WorkspaceEditing::Draw(const PanelText& text, BuildPanel* build, uint32_t centerDockId) {
        UpdateDocuments();
        m_Documents.CloseStaleScenes();
        auto* scene = ActiveScene();
        const auto id = scene ? scene->Id() : StableId{};
        if (m_LastScene != id) {
            m_OverrideRoot = {};
            m_OverridePaths.clear();
            m_Selection.Clear();
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_LastScene = id;
            m_RightLook = false;
            if (scene) {
                m_ResetCamera = true;
                m_Documents.Activate(id);
            }
        }
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
        DrawViewport(scene, text, centerDockId);
        if (auto* asset = dynamic_cast<AssetDocument*>(m_Documents.Find(m_OpenAsset))) {
            bool open = true;
            bool discard = false;
            if (ImGui::Begin(text("panel.asset_editor", "Asset Editor", "Catalpa.AssetEditor").c_str(),
                             &open)) {
                if (ImGui::IsWindowFocused() && m_Documents.Active() != asset)
                    ActivateAsset(*asset);
                if (ImGui::BeginCombo(text("document.assets", "Open drafts", "OpenDrafts").c_str(),
                                      asset->Asset().ToString().c_str())) {
                    for (auto* candidate : m_Documents.Assets()) {
                        auto label = candidate->Asset().ToString() + " / v" +
                                     std::to_string(candidate->SourceVersion());
                        if (candidate->History().Dirty())
                            label += " *";
                        if (ImGui::Selectable(label.c_str(), candidate == asset)) {
                            CancelPendingOpen();
                            m_OpenAsset = candidate->Id();
                            asset = candidate;
                            ActivateAsset(*asset);
                            m_AskDiscard = false;
                        }
                    }
                    ImGui::EndCombo();
                }
                m_AssetEditors.Draw(m_App, *asset, text, m_Error);
                DrawImportSettings(*asset, build, text, m_Error);
                if (m_AskDiscard) {
                    if (ImGui::Button("Save and close") && SaveDocument(*asset))
                        discard = true;
                    ImGui::SameLine();
                    if (ImGui::Button(text("document.discard", "Discard draft and close", "Discard").c_str()))
                        discard = true;
                    ImGui::SameLine();
                    if (ImGui::Button(text("document.keep", "Keep editing", "KeepEdit").c_str()))
                        m_AskDiscard = false;
                }
            }
            ImGui::End();
            if (!open || discard) {
                if (asset->History().Dirty() && !discard)
                    m_AskDiscard = true;
                else {
                    CloseDocument(m_OpenAsset);
                    const auto remaining = m_Documents.Assets();
                    if (!remaining.empty()) {
                        m_OpenAsset = remaining.front()->Id();
                        ActivateAsset(*remaining.front());
                    }
                    m_AskDiscard = false;
                }
            }
        }
        if (!m_Error.empty()) {
            if (ImGui::Begin(text("edit.diagnostic", "Edit diagnostic", "Catalpa.EditDiagnostic").c_str())) {
                ImGui::TextWrapped("%s", m_Error.c_str());
                if (ImGui::Button(text("edit.dismiss", "Dismiss", "Dismiss").c_str()))
                    m_Error.clear();
            }
            ImGui::End();
        }
    }
}
