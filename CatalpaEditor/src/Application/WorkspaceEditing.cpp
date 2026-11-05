#include "Application/WorkspaceEditing.h"
#include "Core/Application.h"
#include "Core/Window.h"
#include "Commands/AssetCommands.h"
#include "Commands/SceneCommands.h"
#include "Panels/BuildPanel.h"
#include "imgui.h"
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
            ImGui::TextWrapped("%s", Tools::AssetPathText(recipe.source).c_str());
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
            ImGui::BeginDisabled(build->Busy());
            if (ImGui::Button(text("edit.reimport", "Rebuild from this draft", "Reimport").c_str()))
                build->Start(*doc.Recipe(), error);
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

    WorkspaceEditing::~WorkspaceEditing() {
        Reset();
    }

    void WorkspaceEditing::Reset() {
        m_Picking.Cancel(m_App);
        m_Gizmo.Cancel(m_Error);
        m_Selection.Clear();
        m_App.CloseEditorPreview(SceneSurface);
        m_AssetEditors.Close(m_App);
        m_Documents.Clear();
        m_OpenAsset = {};
        m_LastScene = {};
        m_Error.clear();
        m_AskDiscard = false;
    }

    bool WorkspaceEditing::HasDirtyDocuments() const {
        return m_Documents.HasDirtyDocuments();
    }

    SceneDocument* WorkspaceEditing::ActiveScene() {
        auto& scenes = m_App.GetScenes();
        const auto handle = scenes.GetActiveHandle();
        if (!handle.HasValue())
            return nullptr;
        return m_Documents.SceneFor(scenes, handle, m_Error);
    }

    void WorkspaceEditing::Menu(const PanelText& text) {
        if (ImGui::BeginMenu(text("menu.edit", "Edit", nullptr).c_str())) {
            auto* doc = m_Documents.Active();
            if (ImGui::MenuItem(text("edit.undo", "Undo", nullptr).c_str(), nullptr, false,
                                doc && doc->History().CanUndo()))
                doc->History().Undo(*doc, m_Error);
            if (ImGui::MenuItem(text("edit.redo", "Redo", nullptr).c_str(), nullptr, false,
                                doc && doc->History().CanRedo()))
                doc->History().Redo(*doc, m_Error);
            ImGui::MenuItem(text("panel.viewport", "Scene Viewport", nullptr).c_str(), nullptr,
                            &ShowViewport);
            ImGui::EndMenu();
        }
    }

    void WorkspaceEditing::SceneTree(const PanelText& text) {
        if (auto* doc = ActiveScene()) {
            const bool edited = m_Hierarchy.Draw(*doc, m_Selection, text, m_Error);
            // 层级中的点击/创建/删除比上一帧的像素读回更新，迟到结果不可覆盖它。
            if (edited)
                m_Picking.Cancel(m_App);
            if (ImGui::IsWindowFocused() || edited)
                m_Documents.Activate(doc->Id());
        }
    }

    bool WorkspaceEditing::Inspect(const PanelText& text) {
        auto* doc = ActiveScene();
        if (doc && m_Selection.Resolve(*doc).HasValue()) {
            if (ImGui::IsWindowFocused())
                m_Documents.Activate(doc->Id());
            return m_Inspector.Draw(*doc, m_Selection, text, m_Error);
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

    void WorkspaceEditing::DrawViewport(SceneDocument* doc, const PanelText& text) {
        bool shown =
            ImGui::Begin(text("panel.viewport", "Scene Viewport", "Catalpa.Viewport").c_str(), &ShowViewport);
        if (!shown || !doc) {
            m_Picking.Cancel(m_App);
            m_App.CloseEditorPreview(SceneSurface);
            m_Gizmo.Cancel(m_Error);
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused())
            m_Documents.Activate(doc->Id());
        m_Gizmo.Toolbar(text);
        DrawSelectedBounds(m_App, *doc, m_Selection, m_Error);
        double gpuMs = 0;
        const auto gpuLabel = text("viewport.gpu_time", "GPU frame (ms)", nullptr);
        if (m_App.GetGpuFrameMilliseconds(gpuMs))
            ImGui::Text("%s: %.2f", gpuLabel.c_str(), gpuMs);
        else
            ImGui::TextDisabled("%s: N/A", gpuLabel.c_str());
        const ImVec2 available = ImGui::GetContentRegionAvail(), origin = ImGui::GetCursorScreenPos();
        const ImVec2 density = ImGui::GetWindowViewport()->FramebufferScale;
        if (available.x < 1 || available.y < 1) {
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_App.CloseEditorPreview(SceneSurface);
            ImGui::End();
            return;
        }
        const Extent2D extent{uint32_t(std::clamp(available.x * density.x, 1.f, 4096.f)),
                              uint32_t(std::clamp(available.y * density.y, 1.f, 4096.f))};
        std::string previewError;
        if (m_App.SetEditorPreview(doc->Handle(), SceneSurface, extent, previewError)) {
            RenderSurfaceSnapshot frame;
            const bool ready = m_App.GetEditorPreview(SceneSurface, frame) && frame.textureToken &&
                               frame.extent == extent &&
                               frame.sceneIdentity == doc->Resolve()->GetWorld().GetIdentity();
            m_Picking.Update(m_App, doc, ready ? &frame : nullptr, m_Selection, m_Error);
            if (ready) {
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(frame.textureToken)), available);
                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool gizmo = m_Gizmo.Draw(*doc, m_Selection, frame.view, origin.x, origin.y,
                                                available.x, available.y, m_Error);
                if (clicked && !gizmo) {
                    const auto mouse = ImGui::GetIO().MousePos;
                    const auto x = uint32_t(std::clamp((mouse.x - origin.x) / available.x * extent.width, 0.f,
                                                       float(extent.width - 1)));
                    const auto y = uint32_t(std::clamp((mouse.y - origin.y) / available.y * extent.height,
                                                       0.f, float(extent.height - 1)));
                    m_Picking.Request(m_App, *doc, frame, x, y, m_Error);
                }
            } else
                m_Gizmo.Cancel(m_Error);
        } else {
            m_Error = std::move(previewError);
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
        }
        ImGui::End();
    }

    void WorkspaceEditing::Draw(const PanelText& text, BuildPanel* build) {
        m_Documents.CloseStaleScenes();
        auto* scene = ActiveScene();
        const auto id = scene ? scene->Id() : StableId{};
        if (m_LastScene != id) {
            m_Selection.Clear();
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_LastScene = id;
            if (scene)
                m_Documents.Activate(id);
        }
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
        if (ShowViewport)
            DrawViewport(scene, text);
        else {
            m_Picking.Cancel(m_App);
            m_Gizmo.Cancel(m_Error);
            m_App.CloseEditorPreview(SceneSurface);
        }
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
                    m_AssetEditors.Close(m_App);
                    m_Documents.Close(m_OpenAsset);
                    m_OpenAsset = {};
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
