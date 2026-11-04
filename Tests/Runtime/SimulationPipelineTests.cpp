#include "TestRunner.h"

#include "Runtime/GameplayModule.h"
#include "Runtime/RuntimeServices.h"
#include "ECS/SystemContext.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Renderer/RenderScene.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Geometry/PrimitiveMesh.h"
#include <cmath>
#include <limits>
#include <memory>

namespace Catalpa::Tests {
    // 验证真实模块注册/场景安全点/提取所有权的连接，使用 CPU 服务而非模拟 GPU。
    void RunSimulationPipelineTests(TestContext& test)
    {
        SceneManager scenes;
        SceneHandle handle;
        test.Check(scenes.CreateScene("Pipeline test", handle).IsSuccess(), "Pipeline creates a scene");
        test.Check(scenes.SetActive(handle).IsSuccess(), "Pipeline activates its scene");
        Scene* scene = scenes.GetScene(handle);
        if (!scene) return;
        const auto persistent = PersistentEntityId::FromWords(0x5330333200000100ULL, 1);
        test.Check(scene->QueueCreate(persistent, "Moving entity", {}).IsSuccess(), "Pipeline queues entity");
        test.Check(scenes.UpdateActive().IsSuccess(), "Pipeline commits initial state");
        FrameTime time;
        time.tickCount = 1;
        RuntimeServices services;
        GameplayModule gameplay;
        unsigned calls = 0;
        SystemDesc system;
        system.id = "test.gameplay";
        system.phase = SystemPhase::Gameplay;
        system.accesses = { { "scene.commands", SystemAccessMode::Write } };
        system.execute = [&](SystemContext& context) {
            ++calls;
            test.Check(scenes.Unload(handle).code == WorldError::Busy, "Running systems cannot unload their scene");
            test.Check(scenes.SetActive(handle).code == WorldError::Busy, "Running systems cannot switch active scene");
            Math::TransformTRS moved;
            moved.translation.x = 3.0f;
            return context.scene.QueueSetLocal(context.scene.Resolve(persistent), moved);
        };
        test.Check(gameplay.Install(*scene, std::move(system)).IsSuccess(), "Gameplay installs a complete system");
        test.Check(gameplay.IsInstalled(), "Gameplay publishes registration only after compilation");
        const auto status = scenes.WithActive([&](Scene& active) {
            SystemContext context{ active, time, services };
            if (const auto executed = active.GetSystems().RunPhase(SystemPhase::Gameplay, context); !executed.IsSuccess()) return executed;
            if (const auto committed = active.CommitPending(); !committed.IsSuccess()) return committed;
            return TransformSystem::Update(active);
        });
        test.Check(status.IsSuccess() && calls == 1, "Gameplay runs exactly once inside protected update");
        RenderFramePacket packet;
        test.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, packet).IsSuccess(), "Pipeline extracts stable values");
        test.Check(packet.transforms.size() == 1 && packet.transforms.front().worldMatrix[3].x == 3.0f, "Committed movement reaches extracted frame");
        RenderScene render;
        test.Check(render.Apply(std::move(packet)).IsSuccess(), "Renderer consumes owned frame");
        test.Check(gameplay.Uninstall(*scene).IsSuccess() && !gameplay.IsInstalled(), "Gameplay uninstalls before scene destruction");
        test.Check(scenes.Unload(handle).IsSuccess(), "Scene unloads after execution scope");
        test.Check(render.GetFrame().transforms.size() == 1 && render.GetFrame().transforms.front().worldMatrix[3].x == 3.0f, "Renderer snapshot survives its source scene");
        RenderFramePacket empty;
        empty.sequence = 2;
        test.Check(render.Apply(std::move(empty)).IsSuccess() && render.GetFrame().transforms.empty(), "New empty frame clears unloaded source");
    }

    // 以真实Scene命令/提取/消费链验证共享CPU几何，不创建窗口或GPU。
    // 非均匀与镜像变换使用独立的已知解析值、切线正交和包围端点作为数学证据。
    void RunGeometrySnapshotTests(TestContext& test)
    {
        const auto near = [](float actual, float expected) {
            return std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-5f * (1.0f + std::abs(expected));
        };
        const auto sameMatrix = [](const Math::Mat4& left, const Math::Mat4& right) {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (left[column][row] != right[column][row]) return false;
            return true;
        };
        // 此夹具没有相机；逐字段比较完整变换/实例，防止失败路径只保住sequence却改写部分数据。
        const auto sameFrame = [&](const RenderFramePacket& left, const RenderFramePacket& right) {
            if (left.sequence != right.sequence || left.sceneIdentity != right.sceneIdentity
                || left.transforms.size() != right.transforms.size() || left.instances.size() != right.instances.size()
                || !left.views.empty() || !right.views.empty()) return false;
            for (size_t i = 0; i < left.transforms.size(); ++i)
                if (left.transforms[i].id != right.transforms[i].id
                    || !sameMatrix(left.transforms[i].worldMatrix, right.transforms[i].worldMatrix)) return false;
            for (size_t i = 0; i < left.instances.size(); ++i) {
                const auto& a = left.instances[i];
                const auto& b = right.instances[i];
                if (a.id != b.id || a.mesh != b.mesh || !sameMatrix(a.worldMatrix, b.worldMatrix)
                    || a.worldBounds.empty != b.worldBounds.empty) return false;
                for (int axis = 0; axis < 3; ++axis) {
                    if (a.worldBounds.minimum[axis] != b.worldBounds.minimum[axis]
                        || a.worldBounds.maximum[axis] != b.worldBounds.maximum[axis]) return false;
                    for (int row = 0; row < 3; ++row)
                        if (a.normalMatrix[axis][row] != b.normalMatrix[axis][row]) return false;
                }
                for (int channel = 0; channel < 4; ++channel)
                    if (a.color[channel] != b.color[channel]) return false;
            }
            return true;
        };
        const auto findInstance = [](const RenderFramePacket& frame, EntityId entity) -> const RenderMeshInstance* {
            const RenderInstanceId id{ entity.world, entity.index, entity.generation };
            for (const auto& instance : frame.instances) if (instance.id == id) return &instance;
            return nullptr;
        };

        SceneManager scenes;
        SceneHandle handle;
        RenderScene render;
        std::weak_ptr<const MeshResource> lifetime;
        test.Check(scenes.CreateScene("Geometry snapshot CPU test", handle).IsSuccess(), "Geometry pipeline creates its source scene");
        test.Check(scenes.SetActive(handle).IsSuccess(), "Geometry pipeline activates its source scene");
        Scene* scene = scenes.GetScene(handle);
        test.Check(scene != nullptr, "Geometry pipeline can borrow its source scene");
        if (!scene) return;
        // 测试用局部引用在此作用域末尾全部释放，仅Scene与渲染快照可以继续保活几何。
        {
            MeshData source;
            const auto generated = CreateCubeMesh(1.0f, source);
            test.Check(generated.IsSuccess(), "Geometry pipeline generates a one-meter CPU cube");
            if (!generated.IsSuccess()) return;
            MeshResourceRef mesh;
            const auto sealed = CreateMeshResource(source, mesh);
            test.Check(sealed.IsSuccess() && mesh != nullptr, "Geometry pipeline seals validated immutable geometry");
            if (!sealed.IsSuccess() || !mesh) return;
            lifetime = mesh;
            const auto firstId = PersistentEntityId::FromWords(0x5330333354455354ULL, 1);
            const auto secondId = PersistentEntityId::FromWords(0x5330333354455354ULL, 2);
            Math::TransformTRS firstLocal;
            firstLocal.translation = { 3.0f, -2.0f, 5.0f };
            firstLocal.scale = { 2.0f, 3.0f, 4.0f };
            Math::TransformTRS secondLocal;
            secondLocal.translation = { -4.0f, 1.0f, 0.0f };
            secondLocal.scale = { -2.0f, 3.0f, 0.5f };
            test.Check(scene->QueueCreate(firstId, "Nonuniform cube", firstLocal).IsSuccess(), "Geometry pipeline queues first transformed entity");
            test.Check(scene->QueueCreate(secondId, "Mirrored cube", secondLocal).IsSuccess(), "Geometry pipeline queues second transformed entity");
            test.Check(scenes.UpdateActive().IsSuccess(), "Geometry pipeline commits entities and derives world transforms");
            const EntityId first = scene->Resolve(firstId), second = scene->Resolve(secondId);
            test.Check(first.HasValue() && second.HasValue(), "Geometry pipeline resolves committed entities");
            if (!first.HasValue() || !second.HasValue()) return;
            MeshRendererComponent renderer{ mesh, { 2.0f, 0.25f, 0.125f, 1.0f }, true };
            test.Check(scene->QueueSetMeshRenderer(first, renderer).IsSuccess(), "Geometry component add queues a retained reference");
            renderer.color = { 0.25f, 1.0f, 0.5f, 1.0f };
            test.Check(scene->QueueSetMeshRenderer(second, renderer).IsSuccess(), "Second geometry component shares the same resource");
            test.Check(!scene->GetWorld().Has<MeshRendererComponent>(first)
                && !scene->GetWorld().Has<MeshRendererComponent>(second), "Queued geometry components are unpublished before the structural safe point");
            RenderFramePacket beforeCommit;
            test.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, beforeCommit).IsSuccess()
                && beforeCommit.instances.empty(), "Extraction before command commit sees no queued geometry");
            test.Check(scenes.UpdateActive().IsSuccess(), "Geometry components publish at the common scene safe point");
            RenderFramePacket initial;
            test.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, initial).IsSuccess(), "Geometry pipeline extracts its first instance snapshot");
            test.Check(initial.instances.size() == 2 && initial.transforms.size() == 2,
                "Geometry snapshot has two instances and the corresponding source transforms");
            const auto* a = findInstance(initial, first);
            const auto* b = findInstance(initial, second);
            test.Check(a != nullptr && b != nullptr, "Geometry records preserve the two entity generations");
            if (!a || !b) return;
            test.Check(a->mesh == mesh && b->mesh == mesh && a->mesh == b->mesh,
                "Multiple instances retain one sealed geometry object instead of copying vertices");
            test.Check(near(a->color.x, 2.0f) && near(a->color.y, 0.25f)
                && near(b->color.x, 0.25f) && near(b->color.y, 1.0f),
                "Queued display parameters are captured by value and finite HDR RGB is preserved");
            test.Check(near(a->normalMatrix[0][0], 0.5f) && near(a->normalMatrix[1][1], 1.0f / 3.0f)
                && near(a->normalMatrix[2][2], 0.25f), "Nonuniform normal matrix has the analytic reciprocal scale");
            test.Check(near(b->normalMatrix[0][0], -0.5f) && near(b->normalMatrix[1][1], 1.0f / 3.0f)
                && near(b->normalMatrix[2][2], 2.0f), "Mirrored normal matrix preserves the signed reciprocal scale");
            const Math::Vec3 localNormal{ 1.0f, 1.0f, 0.0f }, localTangent{ 1.0f, -1.0f, 0.0f };
            test.Check(near(glm::dot(a->normalMatrix * localNormal, Math::Mat3{ a->worldMatrix } * localTangent), 0.0f)
                && near(glm::dot(b->normalMatrix * localNormal, Math::Mat3{ b->worldMatrix } * localTangent), 0.0f),
                "Inverse-transpose normals remain perpendicular to transformed tangents for nonuniform and negative scales");
            const auto containsKnown = [](const Aabb& bounds, const Math::Vec3& minimum, const Math::Vec3& maximum) {
                if (bounds.empty) return false;
                for (int axis = 0; axis < 3; ++axis)
                    if (bounds.minimum[axis] > minimum[axis] || bounds.maximum[axis] < maximum[axis]) return false;
                return true;
            };
            test.Check(containsKnown(a->worldBounds, { 2.0f, -3.5f, 3.0f }, { 4.0f, -0.5f, 7.0f }),
                "One-meter cube bounds enclose independently derived nonuniform world endpoints");
            test.Check(containsKnown(b->worldBounds, { -5.0f, -0.5f, -0.25f }, { -3.0f, 2.5f, 0.25f }),
                "Mirrored bounds sort the signed endpoints and remain conservative");
            // 此处手写非对称shear矩阵，防止纯对角缩放让inverse与inverse-transpose错误实现恰好等价。
            Math::Mat4 shearedWorld{ 1.0f };
            shearedWorld[0][0] = 2.0f;
            shearedWorld[1][0] = 1.0f;
            shearedWorld[1][1] = 3.0f;
            shearedWorld[2][2] = 4.0f;
            shearedWorld[3] = Math::Vec4{ 3.0f, -2.0f, 5.0f, 1.0f };
            RenderMeshInstance sheared;
            const auto shearStatus = BuildRenderMeshInstance(a->id, mesh, shearedWorld, a->color, sheared);
            test.Check(shearStatus.IsSuccess(), "Affine shear remains valid derived geometry state");
            if (shearStatus.IsSuccess()) {
                test.Check(near(sheared.normalMatrix[0][0], 0.5f) && near(sheared.normalMatrix[0][1], -1.0f / 6.0f)
                    && near(sheared.normalMatrix[1][0], 0.0f) && near(sheared.normalMatrix[1][1], 1.0f / 3.0f),
                    "A nonsymmetric shear normal matrix matches independent inverse-transpose coefficients");
                test.Check(near(glm::dot(sheared.normalMatrix * Math::Vec3{ 1.0f, 0.0f, 0.0f },
                    Math::Mat3{ shearedWorld } * Math::Vec3{ 0.0f, 1.0f, 0.0f }), 0.0f),
                    "Shear normal remains perpendicular to the independently chosen transformed tangent");
                test.Check(containsKnown(sheared.worldBounds, { 1.5f, -3.5f, 3.0f }, { 4.5f, -0.5f, 7.0f }),
                    "Shear cube bounds conservatively enclose independently derived endpoint sums");
            }
            test.Check(render.Apply(initial).IsSuccess(), "Renderer validates and owns the extracted geometry snapshot");
            const RenderFramePacket accepted = render.GetFrame();
            const auto rejectPacket = [&](RenderFramePacket bad, std::string_view requirement) {
                bad.sequence = render.GetFrame().sequence + 1;
                test.Check(!render.Apply(std::move(bad)).IsSuccess(), requirement);
                test.Check(sameFrame(render.GetFrame(), accepted), "Rejected geometry packet preserves every accepted transform and instance field");
            };
            {
                auto bad = initial;
                bad.instances.front().id.scene += 1;
                rejectPacket(std::move(bad), "Renderer rejects geometry with a foreign scene identity");
            }
            {
                auto bad = initial;
                bad.instances.push_back(bad.instances.front());
                rejectPacket(std::move(bad), "Renderer rejects duplicated geometry instance identity");
            }
            {
                auto bad = initial;
                const auto missing = bad.instances.front().id;
                for (auto it = bad.transforms.begin(); it != bad.transforms.end(); ++it)
                    if (it->id == missing) { bad.transforms.erase(it); break; }
                rejectPacket(std::move(bad), "Renderer rejects geometry whose source transform is missing");
            }
            {
                auto bad = initial;
                bad.instances.front().worldMatrix[3].x += 1.0f;
                rejectPacket(std::move(bad), "Renderer rejects disagreement between instance and source transform");
            }
            {
                auto bad = initial;
                bad.instances.front().normalMatrix = Math::Mat3{ 1.0f };
                rejectPacket(std::move(bad), "Renderer rejects an identity normal matrix for a nonuniform world transform");
            }
            {
                auto bad = initial;
                bad.instances.front().normalMatrix[0][0] = std::numeric_limits<float>::quiet_NaN();
                rejectPacket(std::move(bad), "Renderer rejects non-finite normal matrix values");
            }
            {
                auto bad = initial;
                bad.instances.front().worldBounds.maximum = bad.instances.front().worldBounds.minimum;
                rejectPacket(std::move(bad), "Renderer rejects validly ordered bounds that do not conservatively enclose the geometry");
            }
            {
                auto bad = initial;
                bad.instances.front().mesh.reset();
                rejectPacket(std::move(bad), "Renderer rejects an instance without retained immutable geometry");
            }
            {
                auto bad = initial;
                bad.instances.front().color.y = std::numeric_limits<float>::infinity();
                rejectPacket(std::move(bad), "Renderer rejects non-finite instance display parameters");
            }
            {
                auto bad = initial;
                bad.sceneIdentity = 0;
                rejectPacket(std::move(bad), "Renderer rejects geometry records without a source scene");
            }

            // 大均匀缩放仍可逆；不能用固定绝对误差把约1e-10的有效法线矩阵当成全0。
            Math::Mat4 largeWorld{ 1.0f };
            largeWorld[0][0] = largeWorld[1][1] = largeWorld[2][2] = 1.0e10f;
            RenderMeshInstance largeInstance;
            const auto largeStatus = BuildRenderMeshInstance(a->id, mesh, largeWorld, a->color, largeInstance);
            test.Check(largeStatus.IsSuccess(), "A large uniform scale remains a well-conditioned geometry transform");
            if (largeStatus.IsSuccess()) {
                test.Check(std::abs(largeInstance.normalMatrix[0][0] / 1.0e-10f - 1.0f) < 1.0e-5f
                    && std::abs(largeInstance.normalMatrix[1][1] / 1.0e-10f - 1.0f) < 1.0e-5f
                    && std::abs(largeInstance.normalMatrix[2][2] / 1.0e-10f - 1.0f) < 1.0e-5f,
                    "Large-scale normal coefficients retain their nonzero relative magnitude");
                auto bad = initial;
                for (auto& transform : bad.transforms)
                    if (transform.id == largeInstance.id) transform.worldMatrix = largeWorld;
                for (auto& instance : bad.instances)
                    if (instance.id == largeInstance.id) { instance = largeInstance; instance.normalMatrix = Math::Mat3{ 0.0f }; }
                rejectPacket(std::move(bad), "Renderer rejects a zero normal matrix even when the required reciprocal scale is tiny");
            }

            renderer.visible = false;
            test.Check(scene->QueueSetMeshRenderer(first, renderer).IsSuccess(), "Visibility change queues through the scene command buffer");
            RenderFramePacket beforeHide;
            test.Check(RenderExtractSystem::Extract(*scene, 2, 640, 480, beforeHide).IsSuccess()
                && beforeHide.instances.size() == 2, "Queued visibility does not change an extraction before the safe point");
            test.Check(scenes.UpdateActive().IsSuccess(), "Visibility change commits at the common safe point");
            RenderFramePacket hidden;
            test.Check(RenderExtractSystem::Extract(*scene, 2, 640, 480, hidden).IsSuccess()
                && hidden.instances.size() == 1 && findInstance(hidden, first) == nullptr && findInstance(hidden, second) != nullptr,
                "Committed visibility hides only its instance while preserving source transforms");
            test.Check(render.Apply(hidden).IsSuccess(), "Renderer replaces old instances with the visibility snapshot");
            test.Check(scene->QueueRemoveMeshRenderer(second).IsSuccess(), "Geometry component removal queues through the scene command buffer");
            test.Check(scene->GetWorld().Has<MeshRendererComponent>(second), "Queued removal preserves the component until commit");
            test.Check(scenes.UpdateActive().IsSuccess(), "Geometry component removal commits at the safe point");
            RenderFramePacket removed;
            test.Check(RenderExtractSystem::Extract(*scene, 3, 640, 480, removed).IsSuccess()
                && removed.instances.empty() && removed.transforms.size() == 2,
                "Removed and hidden components produce no stale geometry instances");
            test.Check(render.Apply(removed).IsSuccess() && render.GetFrame().instances.empty(),
                "An empty instance list clears the previous renderer proxies");

            renderer.visible = true;
            test.Check(scene->QueueSetMeshRenderer(first, renderer).IsSuccess()
                && scene->QueueSetMeshRenderer(second, renderer).IsSuccess(), "Geometry can be enabled and reattached using the retained shared resource");
            test.Check(scenes.UpdateActive().IsSuccess(), "Reattached geometry commits before extraction");
            RenderFramePacket restored;
            test.Check(RenderExtractSystem::Extract(*scene, 4, 640, 480, restored).IsSuccess()
                && restored.instances.size() == 2, "Reattached components restore two geometry instances");
            test.Check(render.Apply(restored).IsSuccess(), "Renderer accepts a newer restored geometry frame");
            Math::TransformTRS singular = firstLocal;
            singular.scale.x = 0.0f;
            test.Check(scene->QueueSetLocal(first, singular).IsSuccess() && scenes.UpdateActive().IsSuccess(),
                "Author zero scale remains a valid scene transform rather than being silently repaired");
            RenderFramePacket preserved = restored;
            test.Check(!RenderExtractSystem::Extract(*scene, 5, 640, 480, preserved).IsSuccess(),
                "A visible singular geometry transform rejects extraction of the whole frame");
            test.Check(sameFrame(preserved, restored), "Failed singular extraction preserves every field of the previous output frame");
            const auto* restoredFirst = findInstance(restored, first);
            test.Check(restoredFirst != nullptr, "Restored snapshot retains the nonsingular first instance");
            if (!restoredFirst) return;
            RenderMeshInstance instanceOutput = *restoredFirst;
            Math::Mat4 singularMatrix{ 1.0f };
            singularMatrix[0][0] = 0.0f;
            RenderFramePacket directBefore;
            directBefore.instances.push_back(instanceOutput);
            const auto instanceFailure = BuildRenderMeshInstance(instanceOutput.id, mesh, singularMatrix, instanceOutput.color, instanceOutput);
            RenderFramePacket directAfter;
            directAfter.instances.push_back(instanceOutput);
            test.Check(!instanceFailure.IsSuccess() && sameFrame(directAfter, directBefore),
                "Direct instance building rejects singular input without changing any existing output field");
            renderer.visible = false;
            test.Check(scene->QueueSetMeshRenderer(first, renderer).IsSuccess() && scenes.UpdateActive().IsSuccess(),
                "The singular entity can be explicitly hidden through the scene safe point");
            RenderFramePacket hiddenSingular;
            test.Check(RenderExtractSystem::Extract(*scene, 5, 640, 480, hiddenSingular).IsSuccess()
                && hiddenSingular.instances.size() == 1 && findInstance(hiddenSingular, second) != nullptr,
                "Explicitly invisible singular geometry does not require an unused inverse normal matrix");
            test.Check(scene->QueueSetLocal(first, firstLocal).IsSuccess(), "Geometry test restores its author transform explicitly");
            renderer.visible = true;
            test.Check(scene->QueueSetMeshRenderer(first, renderer).IsSuccess() && scenes.UpdateActive().IsSuccess(),
                "Geometry test restores visibility without changing the accepted immutable snapshot");
            MeshRendererComponent invalid;
            test.Check(!scene->QueueSetMeshRenderer(first, invalid).IsSuccess(), "Scene rejects null geometry before queuing a component change");
            invalid = renderer;
            invalid.color.w = 1.25f;
            test.Check(!scene->QueueSetMeshRenderer(first, invalid).IsSuccess(), "Scene rejects alpha outside its declared range before queuing");
            // 新生成的测试源对象可释放；密封资源和既有快照不借用该对象的任何vector。
            source = {};
            test.Check(mesh->GetData().vertexCount == 24 && mesh->GetData().GetIndexCount() == 36,
                "Releasing the generated source object does not invalidate sealed geometry");
        }
        test.Check(!lifetime.expired(), "Source scene and accepted renderer snapshot keep the shared geometry alive");
        test.Check(scenes.Unload(handle).IsSuccess(), "Geometry source scene unloads after all structural work has ended");
        test.Check(!lifetime.expired() && render.GetFrame().instances.size() == 2
            && render.GetFrame().instances.front().mesh->GetData().GetIndexCount() == 36,
            "Accepted geometry snapshot remains valid after its source Scene and local owners are destroyed");
        RenderFramePacket empty;
        empty.sequence = render.GetFrame().sequence + 1;
        test.Check(render.Apply(std::move(empty)).IsSuccess() && render.GetFrame().instances.empty()
            && render.GetFrame().transforms.empty(), "A newer empty scene frame clears all previously retained geometry instances");
        test.Check(lifetime.expired(), "The sealed mesh is released after the final accepted snapshot owner is removed");
        const auto acceptedSequence = render.GetFrame().sequence;
        const auto rejectTime = [&](const FrameTime& invalid, std::string_view reason) {
            RenderFramePacket bad;
            bad.sequence = acceptedSequence + 1;
            bad.time = invalid;
            test.Check(!render.Apply(std::move(bad)).IsSuccess()
                && render.GetFrame().sequence == acceptedSequence && render.GetFrame().instances.empty(), reason);
        };
        FrameTime invalid;
        invalid.simulationSeconds = std::numeric_limits<double>::quiet_NaN();
        rejectTime(invalid, "Render snapshots reject NaN simulation time without replacing the accepted frame");
        invalid = {};
        invalid.fixedDeltaSeconds = 0.0;
        rejectTime(invalid, "Render snapshots reject a zero fixed step");
        invalid = {};
        invalid.interpolationAlpha = 1.01;
        rejectTime(invalid, "Render interpolation cannot describe a value beyond the current fixed-step interval");
        invalid = {};
        invalid.firstTick = std::numeric_limits<uint64_t>::max();
        invalid.tickCount = 2;
        rejectTime(invalid, "Render tick intervals cannot wrap the uint64 identity range");
    }

    // 真实Scene安全点到多view帧包：各自选择/尺寸、共享实例、缺失/关闭及来源卸载。
    void RunMultiViewExtractionTests(TestContext& test)
    {
        SceneManager scenes;
        SceneHandle handle;
        test.Check(scenes.CreateScene("Multiple view extraction", handle).IsSuccess(), "Multi-view extraction creates its owned scene");
        Scene* scene = scenes.GetScene(handle);
        if (!scene) return;
        const auto id = [](uint64_t low) { return PersistentEntityId::FromWords(0x5330333400001000ULL, low); };
        Math::TransformTRS moved;
        moved.translation.x = 3;
        test.Check(scene->QueueCreate(id(1), "Primary camera", {}).IsSuccess()
            && scene->QueueCreate(id(2), "Preview camera", moved).IsSuccess()
            && scene->QueueCreate(id(3), "Shared cube", {}).IsSuccess() && scene->CommitPending().IsSuccess(),
            "Multi-view source entities commit before camera and mesh components");
        CameraComponent primary, preview;
        primary.viewId = 11; primary.priority = 1; primary.lens.verticalFovRadians = Math::Pi * 0.5f;
        preview.viewId = 22; preview.priority = 2; preview.lens = primary.lens; preview.lens.reversedZ = true;
        MeshData cube;
        MeshResourceRef mesh;
        test.Check(CreateCubeMesh(1.0f, cube).IsSuccess() && CreateMeshResource(cube, mesh).IsSuccess(), "Multi-view fixture creates a sealed shared mesh");
        test.Check(scene->QueueSetCamera(scene->Resolve(id(1)), primary).IsSuccess()
            && scene->QueueSetCamera(scene->Resolve(id(2)), preview).IsSuccess()
            && scene->QueueSetMeshRenderer(scene->Resolve(id(3)), {mesh}).IsSuccess()
            && scene->CommitPending().IsSuccess() && TransformSystem::Update(*scene).IsSuccess(),
            "Multiple source cameras and the single mesh use the normal scene safety point");
        std::array<RenderViewRequest, 3> requests{{
            {.id=101, .cameraViewId=11, .targetGeneration=5, .viewport={800,400}},
            {.id=202, .cameraViewId=22, .targetGeneration=9, .viewport={300,600}},
            {.id=303, .cameraViewId=11, .targetGeneration=2, .viewport={400,400}}
        }};
        RenderFramePacket packet;
        test.Check(RenderExtractSystem::Extract(*scene, 1, requests, packet).IsSuccess() && packet.views.size() == 3,
            "Explicit view requests produce three cameras including two views of the same source");
        if (packet.views.size() != 3) return;
        test.Check(packet.instances.size() == 1 && packet.instances[0].mesh == mesh
            && packet.views[0].camera == packet.views[2].camera && packet.views[0].camera != packet.views[1].camera,
            "Camera views share one scene geometry snapshot without duplicating instance data");
        test.Check(packet.views[0].id == 101 && packet.views[1].id == 202 && packet.views[2].id == 303
            && packet.views[0].targetGeneration == 5 && packet.views[1].targetGeneration == 9
            && std::abs(packet.views[0].matrices.projection[0][0] - 0.5f) < 1.0e-5f
            && std::abs(packet.views[1].matrices.projection[0][0] - 2.0f) < 1.0e-5f
            && packet.views[1].matrices.position.x == 3 && packet.views[1].lens.reversedZ,
            "Output identity, target generation, aspect and camera pose remain independent");
        RenderScene consumer;
        test.Check(consumer.Apply(packet).IsSuccess(), "RenderScene accepts all validated view matrices and shared instances");
        auto wrongSource = packet;
        wrongSource.sequence = 2;
        wrongSource.views[0].camera = packet.views[1].camera;
        test.Check(ValidateRenderView(wrongSource.views[0]).IsSuccess()
            && !consumer.Apply(wrongSource).IsSuccess() && consumer.GetFrame().sequence == 1
            && consumer.GetFrame().views[0].camera == packet.views[0].camera
            && consumer.GetFrame().instances[0].mesh == mesh,
            "A self-consistent camera cannot claim another source transform or replace the accepted multi-view frame");
        const auto saved = packet;
        auto invalid = requests;
        invalid[1].id = invalid[0].id;
        test.Check(!RenderExtractSystem::Extract(*scene, 2, invalid, packet).IsSuccess()
            && packet.sequence == saved.sequence && packet.views.size() == 3 && packet.instances[0].mesh == mesh,
            "Duplicate output identity preserves the complete previously extracted frame");
        invalid = requests; invalid[1].cameraViewId = 999;
        test.Check(!RenderExtractSystem::Extract(*scene, 2, invalid, packet).IsSuccess() && packet.sequence == 1,
            "A missing explicitly requested enabled camera is not silently replaced by another camera");
        invalid = requests; invalid[1].viewport.width = 0;
        test.Check(RenderExtractSystem::Extract(*scene, 2, invalid, packet).IsSuccess() && packet.views.size() == 2
            && packet.views[0].id == 101 && packet.views[1].id == 303 && packet.instances.size() == 1,
            "Closing a zero-size view removes only that camera while shared scene data remains");
        test.Check(RenderExtractSystem::Extract(*scene, 3, std::span<const RenderViewRequest>{}, packet).IsSuccess()
            && packet.views.empty() && packet.instances.size() == 1, "An empty request list explicitly extracts no output views");
        test.Check(RenderExtractSystem::Extract(*scene, 4, 640, 480, packet).IsSuccess() && packet.views.size() == 1
            && packet.views[0].id == 22, "The existing window entry retains highest-priority source camera identity");
        test.Check(scenes.Unload(handle).IsSuccess() && consumer.GetFrame().views.size() == 3
            && consumer.GetFrame().instances[0].mesh == mesh, "Accepted multi-view frame and sealed geometry survive source scene unload");
    }
}
