#include "TestRunner.h"

#include "Camera/CameraProjection.h"
#include "Camera/Frustum.h"
#include "Panels/BuildPanel.h"
#include "Renderer/RenderScene.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Visibility/VisibilitySystem.h"
#include "Geometry/PrimitiveMesh.h"
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
		/**
		 * @brief 验收比较使用显式混合容差，不改写被比较的数据。
		 *
		 * @param[in] actual 实际观测值。
		 * @param[in] expected 用于比较的期望值。
		 * @param[in] tolerance 允许误差的系数，实际误差上界为 tolerance × (1 + |expected|)。
		 *
		 * @return 两个值均有限且误差处于相对与绝对混合容差以内时为 true。
		 */
		bool Near(float actual, float expected, float tolerance = 2.0e-4f) {
			return std::isfinite(actual) && std::isfinite(expected) &&
			    std::abs(actual - expected) <= tolerance * (1.0f + std::abs(expected));
		}

		bool NearMatrix(const Math::Mat4& a, const Math::Mat4& b) {
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (!Near(a[column][row], b[column][row]))
						return false;
			return true;
		}

		/**
		 * @brief 失败路径要求输出完全保留，不使用容差掩盖部分发布。
		 *
		 * @param[in] a 待逐元素比较的第一组矩阵系数。
		 * @param[in] b 第二组矩阵系数，要求每个对应元素精确相等。
		 *
		 * @return 对应的全部矩阵元素均相等时为 true。
		 */
		bool SameMatrix(const Math::Mat4& a, const Math::Mat4& b) {
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (a[column][row] != b[column][row])
						return false;
			return true;
		}

		/**
		 * @brief 用正距离构造 -Z 相机点，并完成透视除法读取 NDC 深度。
		 *
		 * @param[in] projection 将相机空间坐标变换至裁剪空间的投影矩阵。
		 * @param[in] distance 取样点沿相机负 Z 轴的正距离。
		 *
		 * @return 透视除法后的深度，即裁剪坐标 z / w。
		 */
		float Depth(const Math::Mat4& projection, float distance) {
			const Math::Vec4 clip = projection * Math::Vec4(0.0f, 0.0f, -distance, 1.0f);
			return clip.z / clip.w;
		}

		/**
		 * @brief 直接核对同一 [0,1] clip 条件，作为平面算法之外的点分类参照。
		 *
		 * @param[in] matrix 从输入点所在空间变换至裁剪空间的矩阵。
		 * @param[in] point 通过齐次裁剪不等式测试的三维点。
		 *
		 * @return 点落在带测试容差的裁剪体内时为 true。
		 */
		bool InClip(const Math::Mat4& matrix, const Math::Vec3& point) {
			const Math::Vec4 p = matrix * Math::Vec4(point, 1.0f);
			constexpr float margin = 1.0e-4f;
			return std::isfinite(p.w) && p.w > 0.0f && p.x >= -p.w - margin && p.x <= p.w + margin &&
			    p.y >= -p.w - margin && p.y <= p.w + margin && p.z >= -margin && p.z <= p.w + margin;
		}

		/**
		 * @brief 固定本测试域的作者身份，避免依赖随机生成器或文件保存协议。
		 *
		 * @param[in] low 写入测试身份低 64 位的数值。
		 *
		 * @return 包含本测试固定高位与指定低位的持久实体标识。
		 */
		PersistentEntityId Id(uint64_t low) {
			return PersistentEntityId::FromWords(0x5330333243414D45ULL, low);
		}

		/**
		 * @brief 对可核算投影、深度边界、Y/jitter、解析逆及失败保留建立 CPU 契约。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void CheckProjection(TestContext& context) {
			CameraLens lens;
			lens.verticalFovRadians = Math::Pi / 2.0f;
			lens.nearPlane = 1.0f;
			lens.farPlane = 11.0f;
			CameraViewport viewport{200, 100, 0.0f, 0.0f, false};
			Math::Mat4 projection{1.0f}, inverse{1.0f};
			context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
			    "Finite perspective projection succeeds");
			context.Check(Near(projection[0][0], 0.5f) && Near(projection[1][1], 1.0f) &&
			        Near(projection[2][2], -1.1f) && Near(projection[3][2], -1.1f),
			    "Perspective matrix matches explicit column-vector coefficients");
			context.Check(Near(Depth(projection, 1.0f), 0.0f) && Near(Depth(projection, 11.0f), 1.0f),
			    "Finite forward depth maps near to zero and far to one");
			context.Check(NearMatrix(projection * inverse, Math::Mat4{1.0f}) &&
			        NearMatrix(inverse * projection, Math::Mat4{1.0f}),
			    "Projection inverse is correct in both multiplication orders");
			context.Check(InClip(projection, {4.0f, 2.0f, -2.0f}) && !InClip(projection, {4.1f, 0.0f, -2.0f}) &&
			        !InClip(projection, {0.0f, 2.1f, -2.0f}) && !InClip(projection, {0.0f, 0.0f, -0.5f}) &&
			        !InClip(projection, {0.0f, 0.0f, -12.0f}) && !InClip(projection, {0.0f, 0.0f, 2.0f}),
			    "Clip inequalities reject side, near, far and behind-camera points");

			lens.reversedZ = true;
			context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
			        Near(Depth(projection, 1.0f), 1.0f) && Near(Depth(projection, 11.0f), 0.0f) &&
			        InClip(projection, {0.0f, 0.0f, -2.0f}),
			    "Finite reversed depth preserves the same visibility region");
			for (const bool reversed : {false, true}) {
				lens.reversedZ = reversed;
				lens.infiniteFar = true;
				context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
				        Near(Depth(projection, 1.0f), reversed ? 1.0f : 0.0f) &&
				        Near(Depth(projection, 1000000.0f), reversed ? 0.0f : 1.0f) &&
				        NearMatrix(projection * inverse, Math::Mat4{1.0f}),
				    "Infinite perspective has invertible forward/reversed depth");
				const Math::Vec4 infinityClip = projection * Math::Vec4(0.0f, 0.0f, -1.0f, 0.0f);
				const Math::Vec4 reconstructed = inverse * infinityClip;
				context.Check(Near(reconstructed.z, -1.0f) && Near(reconstructed.w, 0.0f),
				    "Infinite far direction remains a homogeneous direction after inverse");
			}

			lens.infiniteFar = false;
			lens.reversedZ = false;
			viewport.flipY = true;
			viewport.jitterPixelsX = 1.0f;
			viewport.jitterPixelsY = -2.0f;
			context.Check(
			    BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Jittered projection succeeds");
			const auto center = projection * Math::Vec4(0.0f, 0.0f, -2.0f, 1.0f);
			const auto up = projection * Math::Vec4(0.0f, 1.0f, -2.0f, 1.0f);
			context.Check(Near(center.x / center.w, 0.01f) && Near(center.y / center.w, -0.04f) &&
			        up.y / up.w < center.y / center.w && NearMatrix(projection * inverse, Math::Mat4{1.0f}),
			    "Pixel jitter uses target extent and Vulkan Y inversion occurs once in projection");
			viewport = {200, 100, 0.0f, 0.0f, false};
			lens.projection = ProjectionType::Orthographic;
			lens.orthographicHeight = 4.0f;
			for (const bool reversed : {false, true}) {
				lens.reversedZ = reversed;
				context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
				        Near(Depth(projection, 1.0f), reversed ? 1.0f : 0.0f) &&
				        Near(Depth(projection, 11.0f), reversed ? 0.0f : 1.0f) &&
				        NearMatrix(inverse * projection, Math::Mat4{1.0f}) && InClip(projection, {4.0f, 2.0f, -2.0f}) &&
				        !InClip(projection, {4.1f, 0.0f, -2.0f}),
				    "Orthographic aspect, finite depth and inverse follow the same conventions");
			}
			const Math::Mat4 untouched{7.0f};
			projection = untouched;
			inverse = untouched;
			lens.infiniteFar = true;
			context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
			        SameMatrix(projection, untouched) && SameMatrix(inverse, untouched),
			    "Unsupported infinite orthographic projection preserves both outputs");
			lens = {};
			lens.verticalFovRadians = 0.0f;
			context.Check(
			    !BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(), "Zero FOV is rejected");
			lens = {};
			lens.nearPlane = 0.0f;
			context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
			    "Zero perspective near distance is rejected");
			lens = {};
			lens.farPlane = lens.nearPlane;
			context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
			    "Coincident finite depth planes are rejected");
			lens = {};
			viewport.width = 0;
			context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
			    "Direct projection rejects zero target width");
			viewport.width = 200;
			viewport.jitterPixelsX = std::numeric_limits<float>::quiet_NaN();
			context.Check(!BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
			    "Nonfinite jitter is rejected");
			viewport.jitterPixelsX = 0.0f;
			context.Check(!BuildCameraProjection(lens, viewport, projection, projection).IsSuccess() &&
			        SameMatrix(projection, untouched) && SameMatrix(inverse, untouched),
			    "Aliased outputs are rejected and failed builds never partially publish");
		}

		/**
		 * @brief 覆盖相机世界轴、刚性约束、确定性选择、源卸载及值快照生命周期。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void CheckCameraAndExtraction(TestContext& context) {
			SceneManager manager;
			SceneHandle handle;
			context.Check(manager.CreateScene("S03.2 camera snapshot", handle).IsSuccess() &&
			        manager.SetActive(handle).IsSuccess(),
			    "Camera test scene is managed by the unique scene owner");
			Scene* scene = manager.GetScene(handle);
			if (!scene) {
				context.Check(false, "Camera test scene exists");
				return;
			}
			Math::TransformTRS pose;
			pose.translation = {3.0f, 4.0f, 5.0f};
			const float halfAngle = Math::Pi / 4.0f;
			pose.rotation = {std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f};
			context.Check(scene->QueueCreate(Id(10), "camera", pose).IsSuccess() && scene->CommitPending().IsSuccess(),
			    "Create a rigid camera author transform");
			const EntityId camera = scene->Resolve(Id(10));
			CameraComponent component;
			component.viewId = 17;
			context.Check(scene->QueueSetCamera(camera, component).IsSuccess() && scene->CommitPending().IsSuccess() &&
			        TransformSystem::Update(*scene).IsSuccess(),
			    "Camera component and final transform are committed before extraction");
			CameraMatrices matrices;
			context.Check(CameraSystem::Build(*scene, camera, {640, 480}, matrices).IsSuccess(),
			    "Camera system builds from a rigid world pose");
			const auto origin = matrices.view * Math::Vec4(matrices.position, 1.0f);
			const auto forward = matrices.view * Math::Vec4(matrices.position + matrices.forward * 2.0f, 1.0f);
			context.Check(Near(origin.x, 0.0f) && Near(origin.y, 0.0f) && Near(origin.z, 0.0f) &&
			        Near(matrices.forward.x, -1.0f) && Near(matrices.right.z, -1.0f) && Near(matrices.up.y, 1.0f) &&
			        Near(forward.z, -2.0f),
			    "Camera position and local forward obey right-handed world-to-view axes");
			context.Check(NearMatrix(matrices.viewProjection * matrices.inverseViewProjection, Math::Mat4{1.0f}),
			    "Camera combined inverse includes view and projection in reverse order");

			/* 一米近面在 1e8 米位置的 float VP 中会出现明显抵消；有限值不等于可用的逆。 */
			Scene distantScene("Distant camera precision");
			Math::TransformTRS distantPose;
			distantPose.translation = {1.0e8f, 1.0e8f, 1.0e8f};
			CameraComponent distantCamera;
			distantCamera.lens.verticalFovRadians = Math::Pi / 2.0f;
			distantCamera.lens.nearPlane = 1.0f;
			distantCamera.lens.farPlane = 11.0f;
			context.Check(distantScene.QueueCreate(Id(77), "distant camera", distantPose).IsSuccess() &&
			        distantScene.CommitPending().IsSuccess(),
			    "Create a large translated but rigid source camera");
			const EntityId distant = distantScene.Resolve(Id(77));
			context.Check(distantScene.QueueSetCamera(distant, distantCamera).IsSuccess() &&
			        distantScene.CommitPending().IsSuccess() && TransformSystem::Update(distantScene).IsSuccess(),
			    "Large camera translation remains a valid authoritative scene transform");
			CameraMatrices distantOutput;
			distantOutput.viewProjection[0][0] = 7.0f;
			distantOutput.inverseViewProjection[0][0] = 9.0f;
			context.Check(!CameraSystem::Build(distantScene, distant, {640, 480}, distantOutput).IsSuccess() &&
			        distantOutput.viewProjection[0][0] == 7.0f && distantOutput.inverseViewProjection[0][0] == 9.0f,
			    "Final float VP precision failure preserves both combined output matrices");

			RenderFramePacket packet;
			context.Check(RenderExtractSystem::Extract(*scene, 1, 640, 480, packet).IsSuccess() &&
			        packet.views.size() == 1 && packet.transforms.size() == 1 && packet.views.front().id == 17,
			    "Extraction owns camera and transform values with explicit view identity");
			RenderFramePacket minimized;
			context.Check(RenderExtractSystem::Extract(*scene, 2, 0, 0, minimized).IsSuccess() &&
			        minimized.views.empty() && minimized.transforms.size() == 1,
			    "Zero extent skips projection but preserves an owned transform packet");
			CameraMatrices preserved;
			preserved.view[0][0] = 9.0f;
			context.Check(
			    !CameraSystem::Build(*scene, camera, {0, 480}, preserved).IsSuccess() && preserved.view[0][0] == 9.0f,
			    "Direct camera construction rejects zero extent without changing output");

			pose.scale = {2.0f, 1.0f, 1.0f};
			context.Check(scene->QueueSetLocal(camera, pose).IsSuccess() && scene->CommitPending().IsSuccess() &&
			        TransformSystem::Update(*scene).IsSuccess(),
			    "Nonrigid source transform can exist in the scene");
			context.Check(
			    !CameraSystem::Build(*scene, camera, {640, 480}, preserved).IsSuccess() && preserved.view[0][0] == 9.0f,
			    "Camera explicitly rejects inherited scale instead of silently removing it");
			pose.scale = {1.0f, 1.0f, 1.0f};
			context.Check(scene->QueueSetLocal(camera, pose).IsSuccess() &&
			        scene->QueueCreate(Id(5), "tie camera", {}).IsSuccess() && scene->CommitPending().IsSuccess(),
			    "Create a second camera with smaller persistent identity");
			const EntityId tie = scene->Resolve(Id(5));
			CameraComponent tieComponent;
			tieComponent.viewId = 23;
			context.Check(scene->QueueSetCamera(tie, tieComponent).IsSuccess() && scene->CommitPending().IsSuccess() &&
			        TransformSystem::Update(*scene).IsSuccess(),
			    "Commit a camera priority tie");
			context.Check(RenderExtractSystem::Extract(*scene, 3, 640, 480, packet).IsSuccess() &&
			        packet.views.size() == 1 && packet.views.front().id == 23,
			    "Camera priority ties use persistent identity, independent of storage iteration order");
			if (packet.transforms.empty() || packet.views.empty()) {
				context.Check(false, "Extraction produced records required by the lifetime checks");
				return;
			}
			context.Check(manager.Unload(handle).IsSuccess(), "Source scene can unload after synchronous extraction");
			RenderScene consumer;
			context.Check(consumer.Apply(packet).IsSuccess() && consumer.GetFrame().transforms.size() == 2 &&
			        consumer.GetFrame().views.front().id == 23,
			    "Owned render snapshot remains valid after its source scene unloads");
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
			context.Check(consumer.Apply(std::move(empty)).IsSuccess() && consumer.GetFrame().views.empty() &&
			        consumer.GetFrame().transforms.empty(),
			    "Explicit empty packet releases previous scene proxies without owning the source World");
		}

		/**
		 * @brief 数百米移动仍是普通相机用法；用真实Scene提取贯穿相机、快照复核和展示尺寸重建。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void CheckTranslatedCameraPrecision(TestContext& context) {
			Scene scene("Translated camera precision");
			const auto cameraId = Id(91);
			auto status = scene.QueueCreate(cameraId, "Moving camera", {});
			if (status.IsSuccess())
				status = scene.CommitPending();
			CameraComponent camera;
			camera.lens.nearPlane = 0.1f;
			camera.lens.farPlane = 1000.0f;
			if (status.IsSuccess())
				status = scene.QueueSetCamera(scene.Resolve(cameraId), camera);
			if (status.IsSuccess())
				status = scene.CommitPending();
			context.Check(status.IsSuccess(), "Create the ordinary moving-camera precision fixture");
			if (!status.IsSuccess())
				return;

			uint64_t sequence = 1;
			for (const Math::Vec3 position :
			    {Math::Vec3{0, 2, 10}, Math::Vec3{25, 15, 40}, Math::Vec3{-150, 80, 300}, Math::Vec3{400, -250, 700}}) {
				Math::TransformTRS pose;
				pose.translation = position;
				pose.rotation = Math::Quat(Math::Vec3{0.31f, -0.67f, 0.12f});
				status = scene.QueueSetLocal(scene.Resolve(cameraId), pose);
				if (status.IsSuccess())
					status = scene.CommitPending();
				if (status.IsSuccess())
					status = TransformSystem::Update(scene);
				RenderFramePacket packet;
				if (status.IsSuccess())
					status = RenderExtractSystem::Extract(scene, sequence++, 1280, 720, packet);
				context.Check(status.IsSuccess() && packet.views.size() == 1,
				    std::string("Ordinary translated camera publishes a complete snapshot: ") + status.message);
				if (!status.IsSuccess() || packet.views.size() != 1)
					continue;
				const auto& view = packet.views.front();
				context.Check(
				    ValidateRenderView(view).IsSuccess(), "Consumer accepts the same camera precision policy");
				RenderView presented;
				context.Check(BuildPresentationView(view, {600, 1000}, presented).IsSuccess() &&
				        ValidateRenderView(presented).IsSuccess(),
				    "Portrait presentation preserves translated-camera validity");

				/* 独立参照：把相机内一点经cameraWorld送到世界，再由VP投影，NDC应与P直接投影一致。 */
				using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;
				using DVec4 = glm::vec<4, double, glm::packed_highp>;
				const DVec4 local{0.1, -0.1, -2.0, 1.0};
				const DVec4 expected = DMat4(view.matrices.projection) * local;
				const DVec4 actual = DMat4(view.matrices.viewProjection) * (DMat4(view.matrices.inverseView) * local);
				bool matching = actual.w > 0.0;
				for (int axis = 0; axis < 3; ++axis)
					matching &= std::isfinite(actual[axis]) &&
					    std::abs(actual[axis] / actual.w - expected[axis] / expected.w) < 2.0e-4;
				context.Check(matching, "Translated VP retains the independently projected camera-local point");

				auto corrupted = view;
				corrupted.matrices.inverseViewProjection[3][0] += 1.0f;
				context.Check(!ValidateRenderView(corrupted).IsSuccess(),
				    "Scale-aware rounding does not accept an authored inverse-VP mismatch");
			}

			CameraMatrices output;
			output.viewProjection[0][0] = 7.0f;
			output.inverseViewProjection[0][0] = 9.0f;
			Math::Mat4 nonfinite{1.0f};
			nonfinite[3][0] = std::numeric_limits<float>::quiet_NaN();
			context.Check(!BuildCameraMatrices(nonfinite, camera.lens, {1280, 720}, output).IsSuccess() &&
			        output.viewProjection[0][0] == 7.0f && output.inverseViewProjection[0][0] == 9.0f,
			    "Nonfinite independent camera leaves its previously published matrices unchanged");
			Math::Mat4 distant{1.0f};
			distant[3] = {1.0e8f, 1.0e8f, 1.0e8f, 1.0f};
			context.Check(!BuildCameraMatrices(distant, camera.lens, {1280, 720}, output).IsSuccess() &&
			        output.viewProjection[0][0] == 7.0f && output.inverseViewProjection[0][0] == 9.0f,
			    "Scale-aware residual cannot bypass the independent camera world-resolution budget");
			auto illConditioned = camera.lens;
			illConditioned.nearPlane = 1.0e-12f;
			context.Check(!BuildCameraMatrices(Math::Mat4{1.0f}, illConditioned, {1280, 720}, output).IsSuccess() &&
			        output.viewProjection[0][0] == 7.0f && output.inverseViewProjection[0][0] == 9.0f,
			    "Ill-conditioned projection is rejected before any camera output is replaced");
		}

		/**
		 * @brief 独立编辑相机没有伪造的World实体；历史仍绑定真实场景、视图及相机来源类型。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void CheckIndependentCameraHistory(TestContext& context) {
			RenderView independent;
			independent.id = 71;
			independent.historyRevision = 1;
			independent.independentCamera = true;
			independent.viewport = {640, 480};
			context.Check(
			    BuildCameraMatrices(Math::Mat4{1.0f}, independent.lens, independent.viewport, independent.matrices)
			            .IsSuccess() &&
			        ValidateRenderView(independent).IsSuccess(),
			    "Independent camera uses a strictly empty scene-entity identity");
			auto invalid = independent;
			invalid.camera.index = 2;
			context.Check(!ValidateRenderView(invalid).IsSuccess(),
			    "Partially populated independent camera identity is rejected");
			invalid = independent;
			invalid.independentCamera = false;
			context.Check(
			    !ValidateRenderView(invalid).IsSuccess(), "Scene camera cannot omit its real entity identity");

			RenderViewHistory history;
			context.Check(history.Stage(1, 7, {&independent, 1}).IsSuccess(),
			    "Independent view history belongs to its rendered scene");
			history.OnSubmitted();
			context.Check(history.Stage(2, 7, {&independent, 1}).IsSuccess() && history.GetPending()[0].history.valid,
			    "Repeated independent camera preserves its own submitted view history");
			history.OnCancelled();
			auto sceneCamera = independent;
			sceneCamera.independentCamera = false;
			sceneCamera.camera = {7, 0, 1};
			context.Check(history.Stage(2, 7, {&sceneCamera, 1}).IsSuccess() && !history.GetPending()[0].history.valid,
			    "Switching from independent camera to scene camera breaks view continuity");
			history.OnCancelled();
			context.Check(history.Stage(2, 8, {&independent, 1}).IsSuccess() && !history.GetPending()[0].history.valid,
			    "Reusing an independent camera for another scene cannot inherit the former scene history");
		}
	}

	void RunProjectionFrustumTests(TestContext& context) {
		CheckProjection(context);
		CheckCameraAndExtraction(context);
		CheckTranslatedCameraPrecision(context);
		CheckIndependentCameraHistory(context);
	}

	void RunModelPreviewCameraTests(TestContext& context) {
		const Aabb bounds{{-.94745857f, -.900974f, -1.1871551f}, {.9424954f, .900995f, .8128452f}, false};
		for (const CameraViewport viewport : {CameraViewport{1980, 1020}, CameraViewport{600, 1000}}) {
			Scene scene("Model preview camera");
			CameraComponent camera;
			Math::TransformTRS placement;
			std::string error;
			const auto fit = Editor::FitModelPreviewCamera(bounds, viewport, camera.lens, placement, error);
			context.Check(fit, "Fit the preview lens and pose to the model bounds: " + error);
			if (!fit)
				continue;

			auto status = scene.QueueCreate(Id(99), "Preview camera", placement);
			if (status.IsSuccess())
				status = scene.CommitPending();
			if (status.IsSuccess())
				status = scene.QueueSetCamera(scene.Resolve(Id(99)), camera);
			if (status.IsSuccess())
				status = scene.CommitPending();
			if (status.IsSuccess())
				status = TransformSystem::Update(scene);
			RenderFramePacket packet;
			if (status.IsSuccess())
				status = RenderExtractSystem::Extract(scene, 1, viewport.width, viewport.height, packet);
			const bool extracted = status.IsSuccess() && packet.views.size() == 1;
			context.Check(
			    extracted, std::string("Extract the preview camera before scene activation: ") + status.message);
			if (!extracted)
				continue;
			const auto& view = packet.views.front();
			context.Check(
			    ValidateRenderView(view).IsSuccess(), "Preview VP and inverse pass the renderer residual bounds");
			bool enclosed = true;
			for (float x : {bounds.minimum.x, bounds.maximum.x})
				for (float y : {bounds.minimum.y, bounds.maximum.y})
					for (float z : {bounds.minimum.z, bounds.maximum.z})
						enclosed &= InClip(view.matrices.viewProjection, {x, y, z});
			context.Check(enclosed, "All eight model corners fit the preview depth and side planes");

			/* 镜头不变，仅前后移动；前表面中心在两次移动后都应能通过实际 VP 裁剪。 */
			const Math::Vec3 front{placement.translation.x, placement.translation.y, bounds.maximum.z};
			const auto frontVisible = [&](float cameraZ) {
				auto world = view.matrices.inverseView;
				world[3][2] = cameraZ;
				CameraMatrices moved;
				return BuildCameraMatrices(world, camera.lens, viewport, moved).IsSuccess() &&
				    InClip(moved.viewProjection, front);
			};
			context.Check(frontVisible(front.z + 0.1f), "Preview allows a close view ten centimetres from the surface");
			context.Check(
			    frontVisible(placement.translation.z + 8.0f), "Preview keeps the model visible after backing away");
		}

		/* 远离原点的盒子可用较紧的深度范围取景，不能因拓宽导航范围而拒绝原本可见的模型。 */
		const Aabb translated{{99996, -4, -4}, {100004, 4, 4}, false};
		const CameraViewport viewport{1280, 720};
		CameraLens lens;
		Math::TransformTRS pose;
		std::string error;
		const bool fit = Editor::FitModelPreviewCamera(translated, viewport, lens, pose, error);
		Math::Mat4 world;
		CameraMatrices farFromOrigin;
		context.Check(fit && Math::ComposeTRS(pose, world).IsSuccess() &&
		        BuildCameraMatrices(world, lens, viewport, farFromOrigin).IsSuccess() &&
		        InClip(farFromOrigin.viewProjection, {100000, 0, 0}),
		    "A translated preview uses a representable depth range: " + error);
	}

	void RunFrustumVisibilityTests(TestContext& context) {
		CameraLens lens;
		lens.verticalFovRadians = Math::Pi / 2.0f;
		lens.nearPlane = 1.0f;
		lens.farPlane = 11.0f;
		CameraViewport viewport{200, 100, 0.0f, 0.0f, false};
		Math::Mat4 projection{1.0f}, inverse{1.0f};
		Frustum frustum;
		const auto plane = [&](FrustumSide side) -> const FrustumPlane& {
			return frustum.planes[static_cast<size_t>(side)];
		};
		const auto pointBounds = [](Math::Vec3 point) { return Aabb{point, point, false}; };
		const auto classified = [&](const Aabb& bounds, bool expected) {
			bool visible = !expected;
			return IntersectsFrustum(frustum, bounds, visible).IsSuccess() && visible == expected;
		};
		context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
		        BuildFrustum(projection, lens, frustum).IsSuccess(),
		    "Extract finite frustum from the final column-vector projection");
		const auto nearValue = plane(FrustumSide::Near).coefficients;
		const auto farValue = plane(FrustumSide::Far).coefficients;
		const auto leftValue = plane(FrustumSide::Left).coefficients;
		context.Check(Near(static_cast<float>(nearValue[2]), -1.0f) && Near(static_cast<float>(nearValue[3]), -1.0f) &&
		        Near(static_cast<float>(farValue[2]), 1.0f) && Near(static_cast<float>(farValue[3]), 11.0f) &&
		        Near(static_cast<float>(leftValue[0]), 1.0f / std::sqrt(5.0f)) &&
		        Near(static_cast<float>(leftValue[2]), -2.0f / std::sqrt(5.0f)),
		    "Near, far and aspect-dependent side planes match independent analytic unit coefficients");
		context.Check(classified(pointBounds({0, 0, -2}), true) && classified(pointBounds({-4, -2, -2}), true) &&
		        classified(pointBounds({4, 2, -2}), true) && classified(pointBounds({4.1f, 0, -2}), false) &&
		        classified(pointBounds({0, 2.1f, -2}), false) && classified(pointBounds({0, 0, -0.5f}), false) &&
		        classified(pointBounds({0, 0, -12}), false) && classified(pointBounds({0, 0, 2}), false),
		    "Frustum keeps boundaries and rejects all finite clip exclusions");
		context.Check(classified(Aabb{{-20, -20, -20}, {20, 20, 20}, false}, true) &&
		        classified(Aabb{{3.9f, -0.1f, -2.1f}, {5, 0.1f, -1.9f}, false}, true) && classified(Aabb{}, false),
		    "Maximum-support testing retains enclosing and intersecting bounds while empty bounds draw nothing");

		/* 对相同可见体积，reversed-Z仅交换近/远clip条件，不反转平面内部方向。 */
		for (const bool reversed : {false, true}) {
			lens.reversedZ = reversed;
			for (const bool flipY : {false, true}) {
				viewport.flipY = flipY;
				context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
				        BuildFrustum(projection, lens, frustum).IsSuccess() &&
				        classified(pointBounds({0, 0, -1}), true) && classified(pointBounds({0, 0, -11}), true) &&
				        classified(pointBounds({0, 0, -0.99f}), false) &&
				        classified(pointBounds({0, 0, -11.01f}), false) && classified(pointBounds({0, 2, -2}), true),
				    "Finite depth semantics and boundary retention survive reversed-Z and a single Y flip");
				lens.infiniteFar = true;
				context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
				        BuildFrustum(projection, lens, frustum).IsSuccess() && !plane(FrustumSide::Far).active &&
				        plane(FrustumSide::Near).active && classified(pointBounds({0, 0, -1.0e30f}), true) &&
				        classified(pointBounds({0, 0, -0.5f}), false) && classified(pointBounds({0, 0, 1}), false),
				    "Infinite forward/reversed perspective omits only far while keeping near and behind-camera rejection");
				lens.infiniteFar = false;
			}
		}
		lens.reversedZ = false;
		viewport = {200, 100, 0, 0, false};
		lens.projection = ProjectionType::Orthographic;
		lens.orthographicHeight = 4.0f;
		for (const bool reversed : {false, true}) {
			lens.reversedZ = reversed;
			context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
			        BuildFrustum(projection, lens, frustum).IsSuccess() && classified(pointBounds({4, 2, -1}), true) &&
			        classified(pointBounds({-4, -2, -11}), true) && classified(pointBounds({4.1f, 0, -2}), false) &&
			        classified(pointBounds({0, 0, -12}), false),
			    "Orthographic frustum uses constant horizontal and vertical spans for both depth orders");
		}

		lens.projection = ProjectionType::Perspective;
		lens.reversedZ = false;
		viewport.jitterPixelsX = 10.0f; /* 在距离为 2 时，NDC 的水平偏移 +0.1 使世界空间的左右边界同时平移 -0.4。 */
		viewport.jitterPixelsY = -5.0f; /* NDC 的竖直偏移 -0.1 使上下边界同时平移 +0.2。 */
		context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess() &&
		        BuildFrustum(projection, lens, frustum).IsSuccess() && classified(pointBounds({-4.2f, 0, -2}), true) &&
		        classified(pointBounds({3.8f, 0, -2}), false) && classified(pointBounds({0, 2.1f, -2}), true) &&
		        classified(pointBounds({0, -1.9f, -2}), false),
		    "Pixel jitter shifts clip bounds once with the view aspect and final NDC axis signs");
		viewport.jitterPixelsX = viewport.jitterPixelsY = 0;
		context.Check(BuildCameraProjection(lens, viewport, projection, inverse).IsSuccess(),
		    "Rebuild an unjittered projection for world-coordinate checks");
		Math::Mat4 worldToView{0.0f};
		worldToView[0][2] = 1;
		worldToView[1][1] = 1;
		worldToView[2][0] = -1;
		worldToView[3] = {5, -4, -3, 1};
		context.Check(BuildFrustum(projection * worldToView, lens, frustum).IsSuccess() &&
		        classified(pointBounds({1, 4, 5}), true) && classified(pointBounds({5, 4, 5}), false) &&
		        classified(pointBounds({1, 4, 10}), false),
		    "Row extraction correctly includes a translated camera facing world -X");
		context.Check(
		    BuildFrustum(projection * 1.0e-20f, lens, frustum).IsSuccess() && classified(pointBounds({0, 0, -2}), true),
		    "A uniformly scaled homogeneous VP is not rejected by a fixed determinant threshold");

		/* 直接点乘clip作独立参照；取严格内部/外部点，避开GPU float容差形成的保守边缘带。 */
		context.Check(BuildFrustum(projection, lens, frustum).IsSuccess(), "Restore the canonical finite frustum");
		const std::array<Math::Vec3, 5> clipPoints{Math::Vec3{0, 0, -2}, Math::Vec3{-2, -1, -3}, Math::Vec3{10, 0, -2},
		    Math::Vec3{0, 5, -2}, Math::Vec3{0, 0, 3}};
		for (const Math::Vec3 point : clipPoints)
			context.Check(classified(pointBounds(point), InClip(projection, point)),
			    "Frustum point classification agrees with an independent homogeneous clip evaluation");
		const Frustum saved = frustum;
		auto unchanged = [&]() {
			for (size_t i = 0; i < frustum.planes.size(); ++i)
				if (frustum.planes[i].active != saved.planes[i].active ||
				    frustum.planes[i].coefficients != saved.planes[i].coefficients ||
				    frustum.planes[i].absoluteClipCoefficients != saved.planes[i].absoluteClipCoefficients)
					return false;
			return true;
		};
		auto badMatrix = projection;
		badMatrix[0][0] = std::numeric_limits<float>::quiet_NaN();
		context.Check(!BuildFrustum(badMatrix, lens, frustum).IsSuccess() && unchanged() &&
		        !BuildFrustum(Math::Mat4{0}, lens, frustum).IsSuccess() && unchanged(),
		    "Nonfinite or singular VP rejects extraction without partially overwriting planes");
		lens.infiniteFar = true;
		context.Check(!BuildFrustum(projection, lens, frustum).IsSuccess() && unchanged(),
		    "Infinite lens cannot silently discard a finite VP far plane");
		lens.infiniteFar = false;
		bool preserved = true;
		auto badFrustum = saved;
		badFrustum.planes[0].active = false;
		context.Check(!IntersectsFrustum(badFrustum, pointBounds({0, 0, -2}), preserved).IsSuccess() && preserved,
		    "Only far may be inactive and malformed frustum preserves the caller result");
		badFrustum = saved;
		badFrustum.planes[5].coefficients[0] = std::numeric_limits<double>::quiet_NaN();
		context.Check(!IntersectsFrustum(badFrustum, pointBounds({100, 0, -2}), preserved).IsSuccess() && preserved,
		    "Outside classification does not hide a malformed later plane");
		badFrustum = saved;
		badFrustum.planes[0].coefficients[0] *= 2;
		context.Check(!IntersectsFrustum(badFrustum, pointBounds({0, 0, -2}), preserved).IsSuccess() && preserved &&
		        !IntersectsFrustum(Frustum{}, Aabb{}, preserved).IsSuccess() && preserved,
		    "Nonunit planes and a default all-inactive frustum are invalid even with empty bounds");
		context.Check(!IntersectsFrustum(saved, Aabb{{1, 0, 0}, {0, 1, 1}, false}, preserved).IsSuccess() && preserved,
		    "Inverted AABB bounds are diagnosed without changing classification output");

		RenderView view;
		view.lens = lens;
		view.viewport = viewport;
		view.matrices.viewProjection = projection;
		MeshData cubeData, planeData;
		MeshResourceRef cube, ground;
		context.Check(CreateCubeMesh(1.0f, cubeData).IsSuccess() && CreateMeshResource(cubeData, cube).IsSuccess() &&
		        CreatePlaneMesh(1.0f, 1.0f, planeData).IsSuccess() && CreateMeshResource(planeData, ground).IsSuccess(),
		    "Stable grouping uses two sealed CPU mesh identities without GPU ownership");
		if (!cube || !ground)
			return;
		std::vector<RenderMeshInstance> instances(5);
		instances[0].mesh = instances[1].mesh = instances[2].mesh = cube;
		instances[3].mesh = instances[4].mesh = ground;
		instances[0].worldBounds = pointBounds({0, 0, -2});
		instances[1].worldBounds = pointBounds({100, 0, -2});
		instances[2].worldBounds = pointBounds({3, 0, -2});
		instances[3].worldBounds = pointBounds({0.25f, 0, -2});
		instances[4].worldBounds = {};
		VisibilityResult visible;
		context.Check(VisibilitySystem::Build(view, instances, visible).IsSuccess() &&
		        visible.instanceIndices == std::vector<size_t>{0, 2, 3} && visible.groups.size() == 2 &&
		        visible.groups[0].mesh == cube && visible.groups[1].mesh == ground &&
		        visible.groups[0].instanceIndices == std::vector<size_t>{0, 2} &&
		        visible.groups[1].instanceIndices == std::vector<size_t>{3} &&
		        instances[1].worldBounds.minimum.x == 100,
		    "Per-view visibility retains original indices and stable first-occurrence grouping without rewriting source bounds");
		RenderView other = view;
		other.viewport.width = 50;
		context.Check(
		    BuildCameraProjection(other.lens, other.viewport, other.matrices.viewProjection, inverse).IsSuccess() &&
		        VisibilitySystem::Build(other, instances, visible).IsSuccess() &&
		        visible.instanceIndices == std::vector<size_t>{0, 3},
		    "A second view culls independently using its own aspect");
		instances[4].worldBounds.minimum.x = std::numeric_limits<float>::quiet_NaN();
		context.Check(!VisibilitySystem::Build(view, instances, visible).IsSuccess() &&
		        visible.instanceIndices == std::vector<size_t>{0, 3} && visible.groups.size() == 2,
		    "Invalid later instance bounds reject the whole visible-list candidate without partial publication");
		view.viewport.width = 0;
		context.Check(VisibilitySystem::Build(view, instances, visible).IsSuccess() &&
		        visible.instanceIndices.empty() && visible.groups.empty(),
		    "Zero extent skips projection and publishes no visibility work");
	}

}
