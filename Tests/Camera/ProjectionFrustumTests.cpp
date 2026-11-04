#include "TestRunner.h"

#include "Camera/CameraProjection.h"
#include "Renderer/RenderScene.h"
#include "Runtime/Systems/CameraSystem.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scene/Components/CameraComponent.h"

#include <cmath>
#include <limits>
#include <utility>

namespace Catalpa::Tests {
    namespace {
        // 验收比较使用显式混合容差，不改写被比较的数据。
        bool Near(float actual, float expected, float tolerance = 2.0e-4f)
        {
            return std::isfinite(actual) && std::isfinite(expected)
                && std::abs(actual - expected) <= tolerance * (1.0f + std::abs(expected));
        }

        // 逐项检查矩阵，而非依靠单一测试点推断逆矩阵正确。
        bool NearMatrix(const Math::Mat4& a, const Math::Mat4& b)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!Near(a[column][row], b[column][row])) return false;
            return true;
        }

        // 失败路径要求输出完全保留，不使用容差掩盖部分发布。
        bool SameMatrix(const Math::Mat4& a, const Math::Mat4& b)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (a[column][row] != b[column][row]) return false;
            return true;
        }

        // 用正距离构造 -Z 相机点，并完成透视除法读取 NDC 深度。
        float Depth(const Math::Mat4& projection, float distance)
        {
            const Math::Vec4 clip = projection * Math::Vec4(0.0f, 0.0f, -distance, 1.0f);
            return clip.z / clip.w;
        }

        // 直接核对同一 [0,1] clip 条件；Frustum 平面提取/Bounds 剔除仍属 S03.4。
        bool InClip(const Math::Mat4& matrix, const Math::Vec3& point)
        {
            const Math::Vec4 p = matrix * Math::Vec4(point, 1.0f);
            constexpr float margin = 1.0e-4f;
            return std::isfinite(p.w) && p.w > 0.0f && p.x >= -p.w - margin && p.x <= p.w + margin
                && p.y >= -p.w - margin && p.y <= p.w + margin && p.z >= -margin && p.z <= p.w + margin;
        }

        // 固定本测试域的作者身份，避免依赖随机生成器或文件保存协议。
        PersistentEntityId Id(uint64_t low) { return PersistentEntityId::FromWords(0x5330333243414D45ULL, low); }

        // 对可核算投影、深度边界、Y/jitter、解析逆及失败保留建立 CPU 契约。
        void CheckProjection(TestContext& context)
        {
            CameraLens lens;
            lens.verticalFovRadians = Math::Pi / 2.0f;
            lens.nearPlane = 1.0f;
            lens.farPlane = 11.0f;
            CameraViewport viewport{ 200, 100, 0.0f, 0.0f, false };
            Math::Mat4 projection{ 1.0f }, inverse{ 1.0f };
            context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Finite perspective projection succeeds");
            context.Check(Near(projection[0][0], 0.5f) && Near(projection[1][1], 1.0f)
                && Near(projection[2][2], -1.1f) && Near(projection[3][2], -1.1f),
                "Perspective matrix matches explicit column-vector coefficients");
            context.Check(Near(Depth(projection, 1.0f), 0.0f) && Near(Depth(projection, 11.0f), 1.0f),
                "Finite forward depth maps near to zero and far to one");
            context.Check(NearMatrix(projection * inverse, Math::Mat4{ 1.0f })
                && NearMatrix(inverse * projection, Math::Mat4{ 1.0f }), "Projection inverse is correct in both multiplication orders");
            context.Check(InClip(projection, { 4.0f, 2.0f, -2.0f })
                && !InClip(projection, { 4.1f, 0.0f, -2.0f }) && !InClip(projection, { 0.0f, 2.1f, -2.0f })
                && !InClip(projection, { 0.0f, 0.0f, -0.5f }) && !InClip(projection, { 0.0f, 0.0f, -12.0f })
                && !InClip(projection, { 0.0f, 0.0f, 2.0f }), "Clip inequalities reject side, near, far and behind-camera points");

            lens.reversedZ = true;
            context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess()
                && Near(Depth(projection, 1.0f), 1.0f) && Near(Depth(projection, 11.0f), 0.0f)
                && InClip(projection, { 0.0f, 0.0f, -2.0f }), "Finite reversed depth preserves the same visibility region");
            for (const bool reversed : { false, true }) {
                lens.reversedZ = reversed;
                lens.infiniteFar = true;
                context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess()
                    && Near(Depth(projection, 1.0f), reversed ? 1.0f : 0.0f)
                    && Near(Depth(projection, 1000000.0f), reversed ? 0.0f : 1.0f)
                    && NearMatrix(projection * inverse, Math::Mat4{ 1.0f }), "Infinite perspective has invertible forward/reversed depth");
                const Math::Vec4 infinityClip = projection * Math::Vec4(0.0f, 0.0f, -1.0f, 0.0f);
                const Math::Vec4 reconstructed = inverse * infinityClip;
                context.Check(Near(reconstructed.z, -1.0f) && Near(reconstructed.w, 0.0f), "Infinite far direction remains a homogeneous direction after inverse");
            }

            lens.infiniteFar = false;
            lens.reversedZ = false;
            viewport.flipY = true;
            viewport.jitterPixelsX = 1.0f;
            viewport.jitterPixelsY = -2.0f;
            context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Jittered projection succeeds");
            const auto center = projection * Math::Vec4(0.0f, 0.0f, -2.0f, 1.0f);
            const auto up = projection * Math::Vec4(0.0f, 1.0f, -2.0f, 1.0f);
            context.Check(Near(center.x / center.w, 0.01f) && Near(center.y / center.w, -0.04f)
                && up.y / up.w < center.y / center.w && NearMatrix(projection * inverse, Math::Mat4{ 1.0f }),
                "Pixel jitter uses target extent and Vulkan Y inversion occurs once in projection");
            viewport = { 200, 100, 0.0f, 0.0f, false };
            lens.projection = ProjectionType::Orthographic;
            lens.orthographicHeight = 4.0f;
            for (const bool reversed : { false, true }) {
                lens.reversedZ = reversed;
                context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess()
                    && Near(Depth(projection, 1.0f), reversed ? 1.0f : 0.0f)
                    && Near(Depth(projection, 11.0f), reversed ? 0.0f : 1.0f)
                    && NearMatrix(inverse * projection, Math::Mat4{ 1.0f })
                    && InClip(projection, { 4.0f, 2.0f, -2.0f }) && !InClip(projection, { 4.1f, 0.0f, -2.0f }),
                    "Orthographic aspect, finite depth and inverse follow the same conventions");
            }
            const Math::Mat4 untouched{ 7.0f };
            projection = untouched;
            inverse = untouched;
            lens.infiniteFar = true;
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess()
                && SameMatrix(projection, untouched) && SameMatrix(inverse, untouched), "Unsupported infinite orthographic projection preserves both outputs");
            lens = {};
            lens.verticalFovRadians = 0.0f;
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Zero FOV is rejected");
            lens = {};
            lens.nearPlane = 0.0f;
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Zero perspective near distance is rejected");
            lens = {};
            lens.farPlane = lens.nearPlane;
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Coincident finite depth planes are rejected");
            lens = {};
            viewport.width = 0;
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Direct projection rejects zero target width");
            viewport.width = 200;
            viewport.jitterPixelsX = std::numeric_limits<float>::quiet_NaN();
            context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Nonfinite jitter is rejected");
            viewport.jitterPixelsX = 0.0f;
            context.Check(!BuildCameraProjection(lens, viewport, projection, projection).IsSuccess()
                && SameMatrix(projection, untouched) && SameMatrix(inverse, untouched), "Aliased outputs are rejected and failed builds never partially publish");
        }

        // 覆盖相机世界轴、刚性约束、确定性选择、源卸载及值快照生命周期。
        void CheckCameraAndExtraction(TestContext& context)
        {
            SceneManager manager;
            SceneHandle handle;
            context.Check(manager.CreateScene("S03.2 camera snapshot", handle).IsSuccess() && manager.SetActive(handle).IsSuccess(), "Camera test scene is managed by the unique scene owner");
            Scene* scene = manager.GetScene(handle);
            if (!scene) { context.Check(false, "Camera test scene exists"); return; }
            Math::TransformTRS pose;
            pose.translation = { 3.0f, 4.0f, 5.0f };
            const float halfAngle = Math::Pi / 4.0f;
            pose.rotation = { std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f };
            context.Check(scene->QueueCreate(Id(10), "camera", pose).IsSuccess() && scene->CommitPending().IsSuccess(), "Create a rigid camera author transform");
            const EntityId camera = scene->Resolve(Id(10));
            CameraComponent component;
            component.viewId = 17;
            context.Check(scene->QueueSetCamera(camera, component).IsSuccess() && scene->CommitPending().IsSuccess()
                && TransformSystem::Update(*scene).IsSuccess(), "Camera component and final transform are committed before extraction");
            CameraMatrices matrices;
            context.Check(CameraSystem::Build(*scene, camera, { 640, 480 }, matrices).IsSuccess(), "Camera system builds from a rigid world pose");
            const auto origin = matrices.view * Math::Vec4(matrices.position, 1.0f);
            const auto forward = matrices.view * Math::Vec4(matrices.position + matrices.forward * 2.0f, 1.0f);
            context.Check(Near(origin.x, 0.0f) && Near(origin.y, 0.0f) && Near(origin.z, 0.0f)
                && Near(matrices.forward.x, -1.0f) && Near(matrices.right.z, -1.0f) && Near(matrices.up.y, 1.0f)
                && Near(forward.z, -2.0f), "Camera position and local forward obey right-handed world-to-view axes");
            context.Check(NearMatrix(matrices.viewProjection * matrices.inverseViewProjection, Math::Mat4{ 1.0f }), "Camera combined inverse includes view and projection in reverse order");

            // 一米近面在 1e8 米位置的 float VP 中会出现明显抵消；有限值不等于可用的逆。
            Scene distantScene("Distant camera precision");
            Math::TransformTRS distantPose;
            distantPose.translation = { 1.0e8f, 1.0e8f, 1.0e8f };
            CameraComponent distantCamera;
            distantCamera.lens.verticalFovRadians = Math::Pi / 2.0f;
            distantCamera.lens.nearPlane = 1.0f;
            distantCamera.lens.farPlane = 11.0f;
            context.Check(distantScene.QueueCreate(Id(77), "distant camera", distantPose).IsSuccess()
                && distantScene.CommitPending().IsSuccess(), "Create a large translated but rigid source camera");
            const EntityId distant = distantScene.Resolve(Id(77));
            context.Check(distantScene.QueueSetCamera(distant, distantCamera).IsSuccess()
                && distantScene.CommitPending().IsSuccess() && TransformSystem::Update(distantScene).IsSuccess(),
                "Large camera translation remains a valid authoritative scene transform");
            CameraMatrices distantOutput;
            distantOutput.viewProjection[0][0] = 7.0f;
            distantOutput.inverseViewProjection[0][0] = 9.0f;
            context.Check(!CameraSystem::Build(distantScene, distant, { 640, 480 }, distantOutput).IsSuccess()
                && distantOutput.viewProjection[0][0] == 7.0f && distantOutput.inverseViewProjection[0][0] == 9.0f,
                "Final float VP precision failure preserves both combined output matrices");

            RenderFramePacket packet;
            context.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, packet).IsSuccess()
                && packet.views.size() == 1 && packet.transforms.size() == 1 && packet.views.front().id == 17,
                "Extraction owns camera and transform values with explicit view identity");
            RenderFramePacket minimized;
            context.Check(RenderExtractSystem::Extract(*scene, 2, 0, 0, minimized).IsSuccess()
                && minimized.views.empty() && minimized.transforms.size() == 1, "Zero extent skips projection but preserves an owned transform packet");
            CameraMatrices preserved;
            preserved.view[0][0] = 9.0f;
            context.Check(!CameraSystem::Build(*scene, camera, { 0, 480 }, preserved).IsSuccess()
                && preserved.view[0][0] == 9.0f, "Direct camera construction rejects zero extent without changing output");

            pose.scale = { 2.0f, 1.0f, 1.0f };
            context.Check(scene->QueueSetLocal(camera, pose).IsSuccess() && scene->CommitPending().IsSuccess()
                && TransformSystem::Update(*scene).IsSuccess(), "Nonrigid source transform can exist in the scene");
            context.Check(!CameraSystem::Build(*scene, camera, { 640, 480 }, preserved).IsSuccess()
                && preserved.view[0][0] == 9.0f, "Camera explicitly rejects inherited scale instead of silently removing it");
            pose.scale = { 1.0f, 1.0f, 1.0f };
            context.Check(scene->QueueSetLocal(camera, pose).IsSuccess() && scene->QueueCreate(Id(5), "tie camera", {}).IsSuccess()
                && scene->CommitPending().IsSuccess(), "Create a second camera with smaller persistent identity");
            const EntityId tie = scene->Resolve(Id(5));
            CameraComponent tieComponent;
            tieComponent.viewId = 23;
            context.Check(scene->QueueSetCamera(tie, tieComponent).IsSuccess() && scene->CommitPending().IsSuccess()
                && TransformSystem::Update(*scene).IsSuccess(), "Commit a camera priority tie");
            context.Check(RenderExtractSystem::Extract(*scene, 3, 640, 480, packet).IsSuccess()
                && packet.views.size() == 1 && packet.views.front().id == 23, "Camera priority ties use persistent identity, independent of storage iteration order");
            if (packet.transforms.empty() || packet.views.empty()) {
                context.Check(false, "Extraction produced records required by the lifetime checks");
                return;
            }
            context.Check(manager.Unload(handle).IsSuccess(), "Source scene can unload after synchronous extraction");
            RenderScene consumer;
            context.Check(consumer.Apply(packet).IsSuccess() && consumer.GetFrame().transforms.size() == 2
                && consumer.GetFrame().views.front().id == 23, "Owned render snapshot remains valid after its source scene unloads");
            RenderFramePacket stale;
            stale.sequence = 2;
            stale.sceneIdentity = packet.sceneIdentity + 1;
            context.Check(!consumer.Apply(std::move(stale)).IsSuccess() && consumer.GetFrame().sequence == 3,
                "Switching source scene does not permit stale global frame sequences");
            auto invalid = packet;
            invalid.sequence = 4;
            invalid.transforms.front().worldMatrix[0][0] = std::numeric_limits<float>::quiet_NaN();
            context.Check(!consumer.Apply(std::move(invalid)).IsSuccess() && consumer.GetFrame().sequence == 3,
                "Consumer rejects nonfinite packets transactionally");
            RenderFramePacket empty;
            empty.sequence = 4;
            context.Check(consumer.Apply(std::move(empty)).IsSuccess() && consumer.GetFrame().views.empty()
                && consumer.GetFrame().transforms.empty(), "Explicit empty packet releases previous scene proxies without owning the source World");
        }
    }

    // 本步只验投影与 clip 条件；真正的 Frustum/Bounds 剔除实现留 S03.4。
    void RunProjectionFrustumTests(TestContext& context)
    {
        CheckProjection(context);
        CheckCameraAndExtraction(context);
    }

}
