#include "TestRunner.h"
#include "Renderer/RenderView.h"
#include "Renderer/RendererFrameContext.h"
#include "Camera/CameraProjection.h"

namespace Catalpa::Tests {
	namespace {
		/**
		 * @brief 测试姿态仅使用已知平移，避免用被测历史或相机系统生成期望值。
		 *
		 * @param[in,out] view 待更新位姿及对应正、逆视图矩阵的测试视图。
		 * @param[in] position 写入视图的已知世界位置；本用例不引入相机旋转。
		 *
		 * @return 成功生成视图矩阵、投影矩阵及其组合时为 true。
		 */
		bool SetPose(RenderView& view, Math::Vec3 position) {
			view.matrices = {};
			view.matrices.position = position;
			view.matrices.inverseView[3] = Math::Vec4(position, 1.0f);
			view.matrices.view[3] = Math::Vec4(-position, 1.0f);
			if (!BuildCameraProjection(
			        view.lens, view.viewport, view.matrices.projection, view.matrices.inverseProjection)
			        .IsSuccess())
				return false;
			view.matrices.viewProjection = view.matrices.projection * view.matrices.view;
			view.matrices.inverseViewProjection = view.matrices.inverseView * view.matrices.inverseProjection;
			return true;
		}

		/**
		 * @brief 逐项读取快照矩阵，取消/重建时不以近似比较掩盖历史被错误推进。
		 *
		 * @param[in] actual 从当前视图历史中读出的矩阵。
		 * @param[in] expected 用于逐元素精确比对的历史快照矩阵。
		 *
		 * @return 历史矩阵的所有对应分量完全相等时为 true。
		 */
		bool SameHistoryMatrix(const Math::Mat4& actual, const Math::Mat4& expected) {
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (actual[column][row] != expected[column][row])
						return false;
			return true;
		}
	}

	void RunMultiViewTests(TestContext& test) {
		RenderView wide;
		wide.id = 11;
		wide.camera = {7, 1, 1};
		wide.historyRevision = 1;
		wide.viewport = {800, 400};
		wide.lens.verticalFovRadians = Math::Pi * 0.5f;
		wide.lens.nearPlane = 1.0f;
		wide.lens.farPlane = 20.0f;
		RenderView tall = wide;
		tall.id = 22;
		tall.camera.index = 2;
		tall.viewport = {300, 600};
		if (!SetPose(wide, {0, 0, 0}) || !SetPose(tall, {3, 0, 0})) {
			test.Check(false, "Independent view fixtures have valid invertible projections");
			return;
		}
		test.Check(ValidateRenderView(wide).IsSuccess() && ValidateRenderView(tall).IsSuccess(),
		    "Two render views keep independently valid aspect and camera values");
		test.Check(std::abs(wide.matrices.projection[0][0] - 0.5f) < 1.0e-6f &&
		        std::abs(tall.matrices.projection[0][0] - 2.0f) < 1.0e-6f && wide.matrices.position.x == 0 &&
		        tall.matrices.position.x == 3,
		    "Known pi/2 FOV produces x scales 1/2 and 2 for independently sized targets");

		/* 每个失败只改变一个域，避免NaN或identity缺失掩盖真正矩阵检查。 */
		const auto rejects = [&](const RenderView& input, std::string_view requirement) {
			test.Check(!ValidateRenderView(input).IsSuccess(), requirement);
		};
		auto bad = wide;
		bad.id = 0;
		rejects(bad, "Zero logical view identity is rejected");
		bad = wide;
		bad.camera.generation = 0;
		rejects(bad, "Expired camera identity is rejected");
		bad = wide;
		bad.targetGeneration = 0;
		rejects(bad, "Zero target generation cannot name an active render target");
		bad = wide;
		bad.historyRevision = 0;
		rejects(bad, "A history revision must be explicitly valid");
		bad = wide;
		bad.viewport.width = 0;
		rejects(bad, "Zero extent is omitted by extraction and cannot be a drawable view");
		bad = wide;
		bad.lens.verticalFovRadians = 0;
		rejects(bad, "A finite matrix cannot hide an invalid authored lens");
		bad = wide;
		bad.matrices.projection[0][0] *= 2;
		rejects(bad, "Projection is checked against this view's own target aspect");
		bad = wide;
		bad.matrices.viewProjection[3][0] = 1;
		rejects(bad, "Finite but incorrect P times V composition is rejected");
		bad = wide;
		bad.matrices.inverseViewProjection[1][1] *= 2;
		rejects(bad, "An invalid combined inverse cannot enter view history");
		bad = wide;
		bad.matrices.position.x = 1;
		rejects(bad, "Camera position must agree with the inverse-view translation");
		bad = wide;
		bad.matrices.forward = {0, 0, 1};
		rejects(bad, "Camera forward must remain local minus-Z in the rigid world pose");
		bad = wide;
		bad.matrices.inverseView[0][0] = -1;
		rejects(bad, "A mirrored camera world transform is not a rigid right-handed camera");
		bad = wide;
		bad.matrices.view[0][0] = std::numeric_limits<float>::quiet_NaN();
		rejects(bad, "Nonfinite view coefficients are rejected even with a finite projection");
		bad = wide;
		bad.matrices.right.y = std::numeric_limits<float>::infinity();
		rejects(bad, "Nonfinite camera axes cannot bypass view validation");
		for (bool reversed : {false, true}) {
			for (bool infinite : {false, true}) {
				auto variant = wide;
				variant.lens.reversedZ = reversed;
				variant.lens.infiniteFar = infinite;
				variant.viewport.jitterPixelsX = 0.75f;
				variant.viewport.jitterPixelsY = -0.25f;
				test.Check(SetPose(variant, {2, 1, 3}) && ValidateRenderView(variant).IsSuccess(),
				    "Finite/infinite forward/reversed perspective with pixel jitter remains a valid view");
			}
		}
		auto orthographic = wide;
		orthographic.lens.projection = ProjectionType::Orthographic;
		orthographic.lens.nearPlane = 0;
		orthographic.lens.orthographicHeight = 4;
		test.Check(SetPose(orthographic, {0, 2, 3}) && ValidateRenderView(orthographic).IsSuccess(),
		    "Finite orthographic view preserves the same rigid/matrix contract");

		RenderViewHistory history;
		std::array<RenderView, 2> views{wide, tall};
		test.Check(history.Stage(1, 7, views).IsSuccess() && history.GetPending().size() == 2 &&
		        !history.GetPending()[0].history.valid && !history.GetPending()[1].history.valid,
		    "First successful preparation owns both views and has no previous submission");
		history.OnSubmitted();
		test.Check(history.GetPending().empty(),
		    "Submission consumes the staged candidates without retaining a fake pending frame");

		auto movedWide = wide;
		test.Check(SetPose(movedWide, {1, 0, 0}), "Moving a view updates only its own rigid matrices");
		views = {movedWide, tall};
		test.Check(history.Stage(2, 7, views).IsSuccess() && history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid && history.GetPending()[0].history.previousSequence == 1 &&
		        SameHistoryMatrix(
		            history.GetPending()[0].history.previousViewProjection, wide.matrices.viewProjection) &&
		        SameHistoryMatrix(history.GetPending()[1].history.previousViewProjection, tall.matrices.viewProjection),
		    "Camera movement reuses each view's own actual submitted VP rather than another view's matrix");
		auto duplicate = views;
		duplicate[1].id = duplicate[0].id;
		test.Check(!history.Stage(3, 7, duplicate).IsSuccess() && history.GetPending().size() == 2 &&
		        history.GetPending()[0].view.matrices.position.x == 1 &&
		        history.GetPending()[0].history.previousSequence == 1,
		    "Failed duplicate-view preparation preserves the complete previous pending transaction");
		history.OnCancelled();
		test.Check(history.GetPending().empty(), "Cancellation drops only the pending candidates");
		test.Check(history.Stage(3, 7, views).IsSuccess() && history.GetPending()[0].history.previousSequence == 1 &&
		        SameHistoryMatrix(history.GetPending()[0].history.previousViewProjection, wide.matrices.viewProjection),
		    "Cancelled acquire/record work cannot become the next successful frame's history");
		history.OnSubmitted();

		auto resized = movedWide;
		test.Check(BuildPresentationView(movedWide, {400, 400}, resized).IsSuccess(),
		    "Resizing one view derives its own square-target projection");
		views = {resized, tall};
		test.Check(history.Stage(4, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid &&
		        SameHistoryMatrix(history.GetPending()[1].history.previousViewProjection, tall.matrices.viewProjection),
		    "Resizing one target invalidates only that view's history");
		history.OnSubmitted();
		test.Check(history.Stage(4, 7, views).IsSuccess() && history.GetPending()[0].history.valid &&
		        history.GetPending()[0].history.previousSequence == 4,
		    "The same CPU packet may be redrawn after a successful submission");
		history.OnCancelled();

		auto rebuilt = resized;
		++rebuilt.targetGeneration;
		views = {rebuilt, tall};
		test.Check(history.Stage(5, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid,
		    "Target generation replacement invalidates only its corresponding history");
		history.OnCancelled();
		auto cut = resized;
		++cut.historyRevision;
		views = {cut, tall};
		test.Check(history.Stage(5, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid,
		    "An explicit camera cut revision does not reset other views");
		history.OnCancelled();
		auto otherCamera = resized;
		++otherCamera.camera.generation;
		views = {otherCamera, tall};
		test.Check(history.Stage(5, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid,
		    "A new camera generation cannot borrow the old camera's history");
		history.OnCancelled();
		auto changedLens = resized;
		changedLens.lens.reversedZ = true;
		test.Check(SetPose(changedLens, {1, 0, 0}), "Lens-change fixture rebuilds its own projection");
		views = {changedLens, tall};
		test.Check(history.Stage(5, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid,
		    "Depth/lens convention changes cut only the affected view history");
		history.OnCancelled();
		auto jittered = resized;
		jittered.viewport.jitterPixelsX = 0.5f;
		test.Check(SetPose(jittered, {1, 0, 0}),
		    "Jitter fixture rebuilds the current projection without modifying prior matrices");
		views = {jittered, tall};
		test.Check(history.Stage(5, 7, views).IsSuccess() && history.GetPending()[0].history.valid &&
		        SameHistoryMatrix(
		            history.GetPending()[0].history.previousViewProjection, resized.matrices.viewProjection),
		    "Subpixel jitter preserves continuity and retains the true previously submitted jittered VP");
		history.OnCancelled();

		const std::array<RenderView, 1> remaining{tall};
		test.Check(history.Stage(6, 7, remaining).IsSuccess() && history.GetPending().size() == 1 &&
		        history.GetPending()[0].history.valid,
		    "Closing one view leaves the other view's history intact");
		history.OnSubmitted();
		views = {resized, tall};
		test.Check(history.Stage(7, 7, views).IsSuccess() && !history.GetPending()[0].history.valid &&
		        history.GetPending()[1].history.valid,
		    "Reopening a removed view starts fresh while the surviving view remains continuous");
		history.OnCancelled();
		test.Check(!history.Stage(0, 7, views).IsSuccess() && !history.Stage(5, 7, views).IsSuccess(),
		    "Zero or older-than-submitted global sequences are rejected");
		test.Check(!history.Stage(7, 8, views).IsSuccess() && history.GetPending().empty(),
		    "A foreign source scene cannot claim another scene's camera values");
		auto newScene = views;
		for (auto& view : newScene)
			view.camera.scene = 8;
		test.Check(history.Stage(7, 8, newScene).IsSuccess() && !history.GetPending()[0].history.valid &&
		        !history.GetPending()[1].history.valid,
		    "Changing source scenes invalidates every old scene history");
		history.OnSubmitted();
		test.Check(history.Stage(8, 0, {}).IsSuccess() && history.GetPending().empty(),
		    "An empty no-scene submission explicitly closes all view histories");
		history.OnSubmitted();
		test.Check(history.Stage(9, 8, newScene).IsSuccess() && !history.GetPending()[0].history.valid,
		    "Views reopened after an empty successful frame cannot reuse retired histories");
		history.Clear();
		test.Check(history.GetPending().empty() && history.Stage(1, 7, views).IsSuccess() &&
		        !history.GetPending()[0].history.valid,
		    "Clearing the complete rendering lifetime also clears the global sequence domain");
	}
}
