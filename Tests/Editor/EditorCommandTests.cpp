#include "catpch.h"
#include "TestRunner.h"
#include "Commands/SceneCommands.h"
#include "Commands/AssetCommands.h"
#include "Commands/TransformDrag.h"
#include "Application/EditorSelection.h"
#include "Documents/DocumentManager.h"
#include "Assets/BuiltinAssetIds.h"
#include "Materials/MaterialValidation.h"
#include "Runtime/RuntimeRegistration.h"
#include "ECS/ComponentRegistry.h"
#include "Camera/CameraProjection.h"
#include "AssetEditors/AssetEditorRegistry.h"

namespace Catalpa::Tests {
    using namespace Editor;

    namespace {
        // 用真实 SceneManager 和实体代次检查事务；不建立窗口或替身 GPU。
        void CheckScenes(TestContext& t) {
            SceneManager manager;
            SceneHandle handle;
            std::string error;
            const bool made = manager.CreateScene("edit", handle).IsSuccess();
            t.Check(made, "Create edit scene");
            if (!made)
                return;
            auto* scene = manager.GetScene(handle);
            DocumentManager documents;
            auto* doc = documents.SceneFor(manager, handle, error);
            t.Check(doc != nullptr, "Open matching scene document");
            if (!doc)
                return;
            PersistentEntityId root, child;
            const bool populated = CreateAuthorEntity(*doc, "root", {}, root, error) &&
                                   CreateAuthorEntity(*doc, "child", root, child, error);
            t.Check(populated, "Create parent and child using author commands");
            if (!populated)
                return;
            t.Check(!ReparentEntity(*doc, root, child, error),
                    "Reject a reparent cycle without recording a command");

            auto& history = doc->History();
            EditorSelection selection;
            const auto id = scene->Resolve(child);
            t.Check(selection.Select(*doc, id), "Select child by document, world and generation");
            LightComponent light;
            light.parameters.intensity = 64;
            t.Check(EditLight(*doc, child, light, error), "Edit light through a reversible command");
            t.Check(DeleteAuthorEntity(*doc, root, error) && !selection.Resolve(*doc).HasValue(),
                    "Delete subtree invalidates old selection");
            t.Check(history.Undo(*doc, error), "Restore subtree with fixed component data");
            const auto restored = scene->Resolve(child);
            const PersistentEntityId external{StableId::FromWords(37, 7)};
            t.Check(scene->QueueCreate(external, "external", {}, restored).IsSuccess() &&
                        scene->CommitPending().IsSuccess() && !history.Redo(*doc, error) &&
                        scene->Resolve(external).HasValue(),
                    "Redo refuses to delete a subtree modified by another author");
            t.Check(scene->QueueDestroySubtree(scene->Resolve(external)).IsSuccess() &&
                        scene->CommitPending().IsSuccess(),
                    "Remove isolated external edit without consuming document history");
            LightComponent copy;
            RelationshipComponent parent;
            t.Check(restored.HasValue() && restored != id && !selection.Resolve(*doc).HasValue(),
                    "Undo restores persistent ID with a new generation");
            t.Check(scene->GetWorld().Get(restored, copy).IsSuccess() && copy.parameters.intensity == 64 &&
                        scene->GetRelationship(restored, parent).IsSuccess() &&
                        parent.parent == scene->Resolve(root),
                    "Restore light and hierarchy");
            t.Check(history.Undo(*doc, error) && !scene->GetWorld().Has<LightComponent>(restored),
                    "Older command rebinds after document-controlled restore");
            t.Check(history.Redo(*doc, error) && scene->GetWorld().Has<LightComponent>(restored),
                    "Reapply light to the restored entity");
            LightComponent invalid = copy;
            invalid.parameters.intensity = -1;
            t.Check(!EditLight(*doc, child, invalid, error) && history.CanRedo(),
                    "Rejected edit preserves the redo branch");
            t.Check(ReparentEntity(*doc, child, {}, error) && !history.CanRedo(),
                    "New valid edit removes obsolete redo branch");

            EnvironmentComponent fog;
            fog.environment = AssetHandle<EnvironmentAsset>{AssetId::FromWords(37, 83)};
            fog.settings.fogEnabled = true;
            fog.settings.fogDensityPerMeter = 0.04f;
            fog.settings.fogColor = {0.2f, 0.3f, 0.4f};
            EnvironmentComponent observed;
            t.Check(EditEnvironment(*doc, child, fog, error) &&
                        scene->GetWorld().Get(restored, observed).IsSuccess() &&
                        observed.settings.fogEnabled && observed.settings.fogDensityPerMeter == 0.04f &&
                        observed.settings.fogColor == fog.settings.fogColor,
                    "Commit fog author fields with explicit density in inverse metres");
            auto invalidFog = fog;
            invalidFog.settings.fogDensityPerMeter = -1;
            t.Check(!EditEnvironment(*doc, child, invalidFog, error) && history.Undo(*doc, error) &&
                        !scene->GetWorld().Has<EnvironmentComponent>(restored) && history.Redo(*doc, error),
                    "Invalid fog density preserves previous state and a reversible history entry");

            const auto oldId = doc->Id();
            t.Check(manager.Unload(handle).IsSuccess() && !doc->Resolve(),
                    "Unloaded scenes cannot be resolved by documents");
            documents.CloseStaleScenes();
            t.Check(!documents.Find(oldId), "Remove stale document when its scene unloads");
        }

        void CheckIdentityAndCamera(TestContext& t) {
            SceneManager scenes;
            auto schemas = std::make_shared<ComponentRegistry>();
            SceneHandle handle;
            std::string error;
            const bool started = RegisterRuntimeComponentSchemas(*schemas, error) &&
                                 scenes.SetComponentSchemas(schemas).IsSuccess() &&
                                 scenes.CreateScene("properties", handle).IsSuccess();
            t.Check(started, "Create schema-enabled author scene");
            if (!started)
                return;
            SceneDocument doc(StableId::FromWords(38, 1), scenes, handle);
            auto& scene = *doc.Resolve();
            const PersistentEntityId id{StableId::FromWords(38, 2)};
            const bool created =
                scene.QueueCreate(id, "before", {}).IsSuccess() && scene.CommitPending().IsSuccess();
            t.Check(created, "Create fixed identity for property edits");
            if (!created)
                return;
            const auto entity = scene.Resolve(id);
            IdentityComponent identity;
            auto name = [&] {
                (void)scene.GetIdentity(entity, identity);
                return identity.name;
            };
            t.Check(RenameEntity(doc, id, "after", "before", error) && name() == "after" &&
                        identity.persistent == id && scene.Resolve(id) == entity,
                    "Rename preserves persistent identity, slot and generation");
            t.Check(!RenameEntity(doc, id, "stale", "before", error) &&
                        !RenameEntity(doc, id, std::string("x\0y", 3), "after", error) &&
                        doc.History().Undo(doc, error) && name() == "before" &&
                        doc.History().Redo(doc, error) && name() == "after",
                    "Stale or invalid names leave the previous rename reversible");

            CameraComponent camera;
            camera.historyRevision = 4;
            camera.viewId = 17;
            const bool added =
                scene.QueueSetCamera(entity, camera).IsSuccess() && scene.CommitPending().IsSuccess();
            t.Check(added, "Install camera with stable view ID and explicit revision");
            if (!added)
                return;
            auto next = camera;
            next.priority = 2;
            next.lens.nearPlane = 0.25f;
            CameraComponent observed;
            auto state = [&](uint64_t revision, int priority, float nearPlane) {
                return scene.GetWorld().Get(entity, observed).IsSuccess() && observed.viewId == 17 &&
                       observed.historyRevision == revision && observed.priority == priority &&
                       observed.lens.nearPlane == nearPlane;
            };
            t.Check(EditCamera(doc, id, next, error) && state(5, 2, 0.25f),
                    "Camera author edit advances history");
            t.Check(doc.History().Undo(doc, error) && state(6, 0, camera.lens.nearPlane) &&
                        doc.History().Redo(doc, error) && state(7, 2, 0.25f),
                    "Undo and redo each advance camera history while restoring lens fields");
            auto bad = observed;
            bad.lens.nearPlane = 0;
            t.Check(!EditCamera(doc, id, bad, error) && state(7, 2, 0.25f),
                    "Invalid perspective near plane leaves live camera untouched");
            // 单独的投影和逆仍可表达；组合实际相机位姿后，inverseVP 的平移项超出 float。
            t.Check(doc.History().Undo(doc, error) && state(8, 0, camera.lens.nearPlane),
                    "Prepare an intact redo branch before rejecting a world-space projection");
            Math::TransformTRS away;
            away.translation.x = 1024.0f;
            auto fragile = observed;
            fragile.lens.nearPlane = std::ldexp(1.0f, -120);
            Math::Mat4 projection, inverseProjection;
            const bool scalarValid =
                BuildCameraProjection(fragile.lens, {1, 1}, projection, inverseProjection).IsSuccess();
            const bool moved =
                scene.QueueSetLocal(entity, away).IsSuccess() && scene.CommitPending().IsSuccess();
            t.Check(scalarValid && moved && !EditCamera(doc, id, fragile, error) && !error.empty() &&
                        state(8, 0, camera.lens.nearPlane) && doc.History().CanRedo(),
                    "Combined projection overflow rejects the edit but preserves the live camera and redo "
                    "branch");
            t.Check(scene.QueueSetLocal(entity, {}).IsSuccess() && scene.CommitPending().IsSuccess() &&
                        doc.History().Redo(doc, error) && state(9, 2, 0.25f),
                    "Redo of the original camera edit still works after the projection rejection");
            bad = observed;
            ++bad.viewId;
            t.Check(!EditCamera(doc, id, bad, error), "View identity is read-only in inspector commands");
            camera = observed;
            camera.historyRevision = UINT64_MAX - 1;
            const bool exhausted =
                scene.QueueSetCamera(entity, camera).IsSuccess() && scene.CommitPending().IsSuccess();
            ++camera.priority;
            t.Check(exhausted && !EditCamera(doc, id, camera, error),
                    "Refuse a new camera edit without revision space for undo");
        }

        void CheckContinuousDrag(TestContext& t) {
            SceneManager manager;
            SceneHandle handle;
            const bool made = manager.CreateScene("gizmo", handle).IsSuccess();
            t.Check(made, "Create drag test scene");
            if (!made)
                return;
            auto& scene = *manager.GetScene(handle);
            const PersistentEntityId id{StableId::FromWords(37, 1)};
            const bool setup =
                scene.QueueCreate(id, "target", {}).IsSuccess() && scene.CommitPending().IsSuccess();
            t.Check(setup, "Prepare isolated gizmo target");
            if (!setup)
                return;
            SceneDocument doc(StableId::FromWords(37, 2), manager, handle);
            const auto ref = scene.MakeReference(scene.Resolve(id));
            TransformDrag drag;
            std::string e;
            Math::TransformTRS a, b;
            a.translation.x = 2;
            b.translation.x = 5;
            TransformComponent observed;
            auto position = [&] {
                return scene.GetTransform(ref.resolved, observed).IsSuccess() ? observed.local.translation.x
                                                                              : -999.f;
            };
            t.Check(drag.Begin(doc, ref, e) && drag.Preview(a, e) && position() == 2 && drag.Preview(b, e) &&
                        position() == 5 && !doc.History().CanUndo(),
                    "Drag publishes each live transform without creating intermediate history");
            t.Check(drag.Cancel(e) && position() == 0 && !doc.History().Dirty(),
                    "Cancel drag restores starting value");
            t.Check(drag.Begin(doc, ref, e) && drag.Preview(a, e) && drag.Preview(b, e) &&
                        drag.Finish(doc, e) && position() == 5,
                    "Finish drag preserves its final preview");
            t.Check(doc.History().Undo(doc, e) && position() == 0 && !doc.History().CanUndo() &&
                        doc.History().Redo(doc, e) && position() == 5,
                    "Continuous drag is exactly one undo/redo operation");
            t.Check(drag.Begin(doc, ref, e) && drag.Preview(a, e) &&
                        scene.QueueSetLocal(ref.resolved, b).IsSuccess() &&
                        scene.CommitPending().IsSuccess() && !drag.Cancel(e) && position() == 5,
                    "Cancel cannot overwrite an unrelated edit");
        }

        void CheckAssetDraft(TestContext& t) {
            std::string e;
            std::shared_ptr<const MaterialInstance> material;
            const auto shader = AssetId::FromWords(37, 4);
            const bool made = CreateBuiltinMaterialInstance(
                BuiltinMaterialKind::Unlit, BuiltinAssetIds::DefaultUnlitDefinition, 1, shader, material, e);
            AssetVersionRef<MaterialInstance> fixed;
            const bool valid = made && AssetVersionRef<MaterialInstance>::TryCreate(
                                           BuiltinAssetIds::DefaultUnlitInstance, 1, material, fixed, e);
            t.Check(valid, "Prepare immutable material instance for document tests");
            if (!valid)
                return;
            DocumentManager documents;
            JobSystem jobs;
            AssetManager assets(jobs);
            const bool published = RegisterRuntimeAssetLoaders(assets, e) &&
                                   assets.PublishBuiltin(fixed.GetId(), fixed.GetVersion(), fixed.GetShared(),
                                                         sizeof(MaterialInstance), e);
            t.Check(published, "Publish an immutable version for the actual asset editor routing path");
            if (!published)
                return;
            AssetEditorRegistry editors;
            auto* doc = editors.Open(fixed.GetId(), "material_instance", assets, documents, e);
            t.Check(doc != nullptr && doc->TypeKey() == "material_instance",
                    "Open a material document through its registered type");
            if (!doc)
                return;
            const auto markerFactory = +[]() -> std::unique_ptr<IAssetEditor> { return {}; };
            const auto markerOpen = +[](AssetId, const AssetManager&, DocumentManager&,
                                        std::string&) -> AssetDocument* { return nullptr; };
            t.Check(!editors.Register("material_instance", markerFactory, markerOpen, e) &&
                        editors.Open(fixed.GetId(), "material_instance", assets, documents, e) == doc,
                    "Duplicate factory registration retains the existing editor and document");
            t.Check(!editors.Open(fixed.GetId(), "unknown_asset", assets, documents, e) && !e.empty() &&
                        documents.Active() == doc,
                    "Unknown editor type preserves the active document");
            const auto before = doc->Material();
            MaterialStateOverrides unchanged;
            t.Check(ChangeMaterial(*doc, {}, unchanged, e) && doc->Material() != before &&
                        fixed.GetShared() == before && doc->History().Undo(*doc, e) &&
                        doc->Material() == before,
                    "Material draft retains original version across undo");
            auto recipe = std::make_shared<Tools::AssetBuildRequest>();
            doc->AttachRecipe(recipe);
            t.Check(ChangeImportSettings(*doc, 2, false, true, e) && doc->Recipe()->settings.unitScale == 2 &&
                        recipe->settings.unitScale == 1 && documents.HasDirtyDocuments(),
                    "Import options edit a separate draft");
            t.Check(!ChangeImportSettings(*doc, -1, false, true, e) && doc->History().Undo(*doc, e) &&
                        doc->Recipe() == recipe && doc->History().Redo(*doc, e),
                    "Invalid import scale preserves undo/redo");
        }
    }

    void RunEditorCommandTests(TestContext& t) {
        CheckScenes(t);
        CheckIdentityAndCamera(t);
        CheckContinuousDrag(t);
        CheckAssetDraft(t);
    }
}
