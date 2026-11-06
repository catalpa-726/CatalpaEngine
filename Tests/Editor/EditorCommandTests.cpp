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
#include "Viewport/EditorCameraController.h"
#include "Properties/RotationEditorState.h"

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
            // 保留极端镜头拒绝用例，再分开验证“镜头本身有效，仅世界姿态超出精度预算”。
            t.Check(doc.History().Undo(doc, error) && state(8, 0, camera.lens.nearPlane),
                    "Prepare an intact redo branch before rejecting a world-space projection");
            Math::TransformTRS away;
            away.translation.x = 1024.0f;
            auto fragile = observed;
            fragile.lens.nearPlane = std::ldexp(1.0f, -120);
            Math::Mat4 projection, inverseProjection;
            t.Check(!BuildCameraProjection(fragile.lens, {1, 1}, projection, inverseProjection).IsSuccess() &&
                        !EditCamera(doc, id, fragile, error) && state(8, 0, camera.lens.nearPlane) && doc.History().CanRedo(),
                    "An ill-conditioned lens cannot replace the camera or its redo branch");
            fragile.lens.nearPlane = 0.05f;
            const bool scalarValid =
                BuildCameraProjection(fragile.lens, {1, 1}, projection, inverseProjection).IsSuccess();
            const bool moved =
                scene.QueueSetLocal(entity, away).IsSuccess() && scene.CommitPending().IsSuccess();
            t.Check(scalarValid && moved && !EditCamera(doc, id, fragile, error) && !error.empty() &&
                        state(8, 0, camera.lens.nearPlane) && doc.History().CanRedo(),
                    "Insufficient world resolution rejects the edit but preserves the live camera and redo "
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

        void CheckRotationEditor(TestContext& t) {
            SceneManager scenes;
            SceneHandle handle;
            const PersistentEntityId id{StableId::FromWords(38, 21)};
            const bool made = scenes.CreateScene("Euler properties", handle).IsSuccess();
            t.Check(made, "Create a scene for rotation author edits");
            if (!made)
                return;
            auto& scene = *scenes.GetScene(handle);
            const bool created = scene.QueueCreate(id, "rotating", {}).IsSuccess() &&
                                 scene.CommitPending().IsSuccess();
            t.Check(created, "Create a stable rotation target");
            if (!created)
                return;
            SceneDocument doc(StableId::FromWords(38, 22), scenes, handle);
            const auto target = scene.MakeReference(scene.Resolve(id));
            std::string error;

            // 独立地按轴乘法构造期望值，避免仅测试一对可能同时错误的编解码函数。
            const auto expected = [](const std::array<double, 3>& degrees) {
                constexpr double factor = 3.14159265358979323846 / 180.0;
                const auto x = glm::angleAxis(degrees[0] * factor, glm::dvec3{1, 0, 0});
                const auto y = glm::angleAxis(degrees[1] * factor, glm::dvec3{0, 1, 0});
                const auto z = glm::angleAxis(degrees[2] * factor, glm::dvec3{0, 0, 1});
                const auto q = glm::normalize(z * y * x);
                return Math::Quat{float(q.w), float(q.x), float(q.y), float(q.z)};
            };
            const auto sameRotation = [](const Math::Quat& a, const Math::Quat& b) {
                for (const auto axis : {Math::Vec3{1, 0, 0}, Math::Vec3{0, 1, 0}, Math::Vec3{0, 0, 1}})
                    if (glm::length(a * axis - b * axis) > 2.e-5f)
                        return false;
                return true;
            };

            const std::array<std::array<double, 3>, 9> examples{{
                {90, 0, 0}, {0, 90, 0}, {0, 0, 90}, {20, 90, 30}, {20, -90, 30},
                {20, 89.999, 30}, {20, -89.999, 30}, {120, 100, -200}, {720, -450, 540}}};
            for (const auto& angles : examples) {
                RotationEditorState state;
                Math::Quat rotation{1, 0, 0, 0}, roundTrip{1, 0, 0, 0};
                const auto source = expected(angles);
                const bool synced = state.Synchronize(doc.Id(), target, source, error);
                t.Check(synced && state.ToQuaternion(roundTrip, error) && sameRotation(source, roundTrip),
                        "Euler decomposition preserves orientation at and near both gimbal singularities");
                state.Degrees() = angles;
                t.Check(state.ToQuaternion(rotation, error) && sameRotation(source, rotation),
                        "XYZ degree input follows explicit Rz Ry Rx composition including whole turns");
            }

            RotationEditorState wrapped;
            t.Check(wrapped.Synchronize(doc.Id(), target, expected({0, 0, 179}), error) &&
                        wrapped.Synchronize(doc.Id(), target, expected({0, 0, -179}), error) &&
                        std::abs(wrapped.Degrees()[2] - 181) < .001,
                    "External rotations crossing 180 degrees keep a continuous display angle");

            RotationEditorState state;
            Math::Quat rotation{1, 0, 0, 0};
            t.Check(state.Synchronize(doc.Id(), target, Math::Quat{1, 0, 0, 0}, error),
                    "Initialize rotation UI draft");
            state.Degrees() = {400, 89, 10};
            const bool converted = state.ToQuaternion(rotation, error);
            state.Accept(rotation);
            const auto revision = state.Revision();
            const auto authoredAngles = state.Degrees();
            t.Check(converted && state.Synchronize(doc.Id(), target, rotation, error) &&
                        state.Synchronize(doc.Id(), target, -rotation, error) &&
                        state.Degrees() == authoredAngles && state.Revision() == revision,
                    "Own commits and equivalent quaternion signs preserve whole-turn UI angles");

            const auto external = expected({41, 91, 11});
            t.Check(state.Synchronize(doc.Id(), target, external, error) && state.Revision() != revision &&
                        std::abs(state.Degrees()[0] - 401) < .001 &&
                        std::abs(state.Degrees()[1] - 91) < .001 &&
                        std::abs(state.Degrees()[2] - 11) < .001,
                    "External Gizmo changes cross 90 degrees without a 180-degree display branch jump");
            const auto singular = expected({42, 90, 12});
            t.Check(state.Synchronize(doc.Id(), target, singular, error) &&
                        state.ToQuaternion(rotation, error) && sameRotation(rotation, singular),
                    "Singular external rotations keep a nearby equivalent Euler representation");
            const auto beforeDraft = state.Revision();
            state.Degrees()[0] += 7;
            const auto draft = state.Degrees();
            t.Check(state.Synchronize(doc.Id(), target, singular, error) && state.Degrees() == draft &&
                        state.Revision() == beforeDraft,
                    "An unchanged scene does not discard a pending angle draft on the next frame");
            t.Check(state.Synchronize(StableId::FromWords(38, 23), target, external, error) &&
                        state.Revision() != beforeDraft && std::abs(state.Degrees()[0]) < 180,
                    "Switching documents starts a fresh display rather than reusing another draft");
            const auto previousTarget = state.Revision();
            t.Check(scene.QueueDestroySubtree(target.resolved).IsSuccess() && scene.CommitPending().IsSuccess() &&
                        scene.QueueCreate(id, "replacement", {}).IsSuccess() && scene.CommitPending().IsSuccess() &&
                        state.Synchronize(doc.Id(), scene.MakeReference(scene.Resolve(id)),
                                          Math::Quat{1, 0, 0, 0}, error) &&
                        state.Revision() != previousTarget && state.Degrees() == std::array<double, 3>{},
                    "Recreated entity generations cannot inherit the previous target's Euler draft");

            state.Degrees() = {400, 91, -190};
            Math::TransformTRS before, after;
            TransformComponent observed;
            const bool committed = state.ToQuaternion(after.rotation, error) &&
                                   ChangeLocal(doc, id, before, after, error);
            state.Accept(after.rotation);
            const auto replacement = scene.MakeReference(scene.Resolve(id));
            t.Check(committed && doc.History().Undo(doc, error) &&
                        scene.GetTransform(replacement.resolved, observed).IsSuccess() &&
                        state.Synchronize(doc.Id(), replacement, observed.local.rotation, error) &&
                        state.ToQuaternion(rotation, error) && sameRotation(rotation, before.rotation) &&
                        !doc.History().CanUndo(),
                    "A rotation author commit remains one undo command and resynchronizes its degree display");
            t.Check(doc.History().Redo(doc, error) &&
                        scene.GetTransform(replacement.resolved, observed).IsSuccess() &&
                        state.Synchronize(doc.Id(), replacement, observed.local.rotation, error) &&
                        state.ToQuaternion(rotation, error) && sameRotation(rotation, after.rotation),
                    "Redo restores the quaternion without storing Euler angles in the scene");
            const auto untouched = rotation;
            state.Degrees()[1] = std::numeric_limits<double>::quiet_NaN();
            t.Check(!state.ToQuaternion(rotation, error) && rotation == untouched && !error.empty(),
                    "Nonfinite degree input fails without replacing the valid quaternion");
        }

        void CheckEditorCamera(TestContext& t) {
            Scene scene("Independent view");
            std::string error;
            EditorCameraController editor;
            const bool fallback = editor.Reset(&scene, error);
            t.Check(fallback && editor.Pose().translation == Math::Vec3(0, 2, 10),
                    "Empty scene opens with an independent usable view");
            const PersistentEntityId cameraId{StableId::FromWords(38, 4)};
            Math::TransformTRS authored;
            authored.translation = {3, 4, 5};
            CameraComponent camera;
            camera.lens.nearPlane = .3f;
            const bool made = scene.QueueCreate(cameraId, "Source camera", authored).IsSuccess() &&
                              scene.CommitPending().IsSuccess() &&
                              scene.QueueSetCamera(scene.Resolve(cameraId), camera).IsSuccess() &&
                              scene.CommitPending().IsSuccess();
            t.Check(made && editor.Reset(&scene, error) &&
                        editor.Pose().translation == authored.translation && editor.Lens() == camera.lens,
                    "Reset inherits the source camera pose and lens before the first render");
            if (!made)
                return;
            const auto history = editor.HistoryRevision();
            t.Check(editor.Navigate({.right = 1, .forward = 1, .seconds = .2f}, error) &&
                        std::abs(glm::length(editor.Pose().translation - authored.translation) - .6f) <
                            1.e-4f,
                    "Diagonal navigation has the same speed as a single axis");
            const auto beforeLook = editor.Pose();
            t.Check(editor.Navigate({.lookX = 20, .lookY = 10, .seconds = 0}, error) &&
                        editor.Pose().rotation != beforeLook.rotation &&
                        editor.Pose().translation == beforeLook.translation,
                    "Mouse look uses pixel delta independently of simulation time");

            // 转动后再滚动，以区分视线方向与固定世界轴；半刻度也应产生位移。
            const auto start = editor.Pose();
            const auto forward = start.rotation * Math::Vec3{0, 0, -1};
            auto timedView = editor;
            EditorCameraInput scroll;
            scroll.wheel = .5f;
            const bool advanced = editor.Navigate(scroll, error);
            const auto movement = editor.Pose().translation - start.translation;
            const float alongView = glm::dot(movement, forward);
            t.Check(advanced && alongView > 0 && glm::length(movement - alongView * forward) < 1.e-4f,
                    "Fractional wheel input dollies along the rotated view even at zero frame time");
            scroll.seconds = .2f;
            t.Check(timedView.Navigate(scroll, error) &&
                        glm::length(timedView.Pose().translation - editor.Pose().translation) < 1.e-4f,
                    "A wheel step has the same displacement at different frame times");
            const auto afterForward = editor.Pose().translation;
            scroll.wheel = -.5f;
            t.Check(editor.Navigate(scroll, error) &&
                        glm::dot(editor.Pose().translation - afterForward, forward) < 0 &&
                        editor.Pose().rotation == start.rotation &&
                        editor.Lens().verticalFovRadians == camera.lens.verticalFovRadians,
                    "Scrolling back retreats without rotating or changing the field of view");

            TransformComponent original;
            t.Check(scene.GetTransform(scene.Resolve(cameraId), original).IsSuccess() &&
                        original.local.translation == authored.translation &&
                        editor.HistoryRevision() == history,
                    "Navigation changes only the editor view and keeps continuous camera history");
            const auto beforeInvalid = editor.Pose();
            t.Check(!editor.Navigate({.forward = 1, .seconds = -1}, error) &&
                        editor.Pose().translation == beforeInvalid.translation,
                    "Invalid camera input preserves the previous independent pose");
            scroll.wheel = std::numeric_limits<float>::quiet_NaN();
            t.Check(!editor.Navigate(scroll, error) && editor.Pose().translation == beforeInvalid.translation,
                    "Invalid wheel input preserves the previous independent pose");
            t.Check(editor.Reset(&scene, error) && editor.Pose().translation == authored.translation &&
                        editor.HistoryRevision() > history,
                    "Explicit reset realigns the view and invalidates prior camera history");
            authored.translation.x = 1.0e8f;
            t.Check(scene.QueueSetLocal(scene.Resolve(cameraId), authored).IsSuccess() &&
                        scene.CommitPending().IsSuccess() && editor.Reset(&scene, error) &&
                        editor.Pose().translation == Math::Vec3(0, 2, 10) && !error.empty(),
                    "An unrepresentable scene camera leaves a usable default editor view and a diagnostic");
        }

        void CheckOrthographicWheel(TestContext& t) {
            Scene scene("Orthographic view");
            const PersistentEntityId id{StableId::FromWords(38, 5)};
            CameraComponent source;
            source.lens.projection = ProjectionType::Orthographic;
            source.lens.orthographicHeight = 12.f;
            EditorCameraController editor;
            std::string error;
            const bool ready = scene.QueueCreate(id, "Source camera", {}).IsSuccess() &&
                               scene.CommitPending().IsSuccess() &&
                               scene.QueueSetCamera(scene.Resolve(id), source).IsSuccess() &&
                               scene.CommitPending().IsSuccess() && editor.Reset(&scene, error) &&
                               editor.Lens() == source.lens;
            t.Check(ready, "Prepare an independent orthographic view");
            if (!ready)
                return;

            const auto pose = editor.Pose();
            CameraComponent observed;
            t.Check(editor.Navigate({.wheel = .5f}, error) &&
                        editor.Lens().orthographicHeight < source.lens.orthographicHeight &&
                        editor.Pose().translation == pose.translation && editor.Pose().rotation == pose.rotation &&
                        scene.GetWorld().Get(scene.Resolve(id), observed).IsSuccess() && observed.lens == source.lens,
                    "Orthographic wheel zooms the editor lens without moving the view or editing the source lens");
            t.Check(editor.Navigate({.wheel = -.5f}, error) &&
                        std::abs(editor.Lens().orthographicHeight - source.lens.orthographicHeight) < 1.e-4f,
                    "The opposite wheel step restores orthographic framing");
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
        CheckRotationEditor(t);
        CheckEditorCamera(t);
        CheckOrthographicWheel(t);
        CheckContinuousDrag(t);
        CheckAssetDraft(t);
    }
}
