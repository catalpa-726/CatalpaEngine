#include "Viewport/TransformGizmo.h"

#include "Application/WorkspaceSettings.h"
#include "Panels/FormLayout.h"
#include "Viewport/ToolbarButton.h"
#include "ImGuizmo.h"

namespace Catalpa::Editor {
	Math::MathStatus SnapGizmoScale(const Math::Vec3& startLocal, const Math::Mat4& startWorld,
	    const Math::Mat4& draggedWorld, float percent, Math::Vec3& result) {
		if (!std::isfinite(percent) || percent < ViewportSettings::MinScaleStepPercent ||
		    percent > ViewportSettings::MaxScaleStepPercent)
			return {Math::MathError::OutOfRange, "Invalid scale snap increment"};
		const double step = double(percent) / 100.0;
		Math::Vec3 scale;
		for (int i = 0; i < 3; ++i) {
			const double start = glm::length(glm::dvec3(startWorld[i]));
			const double dragged = glm::length(glm::dvec3(draggedWorld[i]));
			if (!(start > 0.0) || !std::isfinite(start) || !std::isfinite(dragged))
				return {Math::MathError::Singular, "Cannot snap scale on a degenerate axis"};
			/* 以静止倍率 1 为格点。限制至正档位，防止放手时得到奇异矩阵。 */
			const double ticks = std::max(std::round((dragged / start - 1.0) / step), std::ceil(-0.999 / step));
			const double value = double(startLocal[i]) * (1.0 + ticks * step);
			if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
				return {Math::MathError::OutOfRange, "Snapped scale exceeds the transform range"};
			scale[i] = float(value);
		}
		result = scale;
		return {};
	}

	void TransformGizmo::Toolbar(const PanelText& text, ViewportSettings& settings) {
		const ToolIcon icons[]{ToolIcon::Select, ToolIcon::Move, ToolIcon::Rotate, ToolIcon::Scale};
		const char* keys[]{"gizmo.select", "gizmo.move", "gizmo.rotate", "gizmo.scale"};
		const char* names[]{"Select", "Move", "Rotate", "Scale"};
		for (int i = 0; i < 4; ++i) {
			if (i)
				ImGui::SameLine();
			ImGui::PushID(keys[i]);
			if (ToolbarButton("##tool", icons[i], text(keys[i], names[i], nullptr), m_Operation == i - 1))
				m_Operation = i - 1;
			ImGui::PopID();
		}
		ImGui::SameLine();
		if (ImGui::Button(text(m_Mode ? "gizmo.world" : "gizmo.local", m_Mode ? "World" : "Local", "Space").c_str()))
			m_Mode = !m_Mode;
		ImGui::SameLine();
		if (ToolbarButton("##snap", ToolIcon::Snap, text("gizmo.snap", "Snap", nullptr), settings.snapEnabled))
			settings.snapEnabled = !settings.snapEnabled;
		ImGui::OpenPopupOnItemClick("SnapOptions", ImGuiPopupFlags_MouseButtonRight);
		ImGui::SameLine(0, 1);
		if (ImGui::ArrowButton("##SnapOptions", ImGuiDir_Down))
			ImGui::OpenPopup("SnapOptions");
		ImGui::SetItemTooltip("%s", text("gizmo.snap_options", "Snap increments", nullptr).c_str());
		ImGui::SetNextWindowSize(
		    {std::min(ImGui::GetFontSize() * 18.f, ImGui::GetWindowViewport()->WorkSize.x), 0}, ImGuiCond_Appearing);
		if (ImGui::BeginPopup("SnapOptions")) {
			const auto field = [](const std::string& label, const char* id, float& value, float minimum,
			                       float maximum) {
				PlaceFormLabel(label);
				float edited = value;
				if (ImGui::InputFloat(id, &edited, 0.f, 0.f, "%.6g") && std::isfinite(edited))
					value = std::clamp(edited, minimum, maximum);
			};
			field(text("gizmo.translation_step", "Move (m)", nullptr), "##TranslationStep", settings.translationStep,
			    ViewportSettings::MinTranslationStep, ViewportSettings::MaxTranslationStep);
			field(text("gizmo.rotation_step", "Rotate (°)", nullptr), "##RotationStep", settings.rotationStep,
			    ViewportSettings::MinRotationStep, ViewportSettings::MaxRotationStep);
			field(text("gizmo.scale_step_percent", "Scale (% of starting size)", nullptr), "##ScaleStepPercent",
			    settings.scaleStepPercent, ViewportSettings::MinScaleStepPercent,
			    ViewportSettings::MaxScaleStepPercent);
			ImGui::EndPopup();
		}
	}

	bool TransformGizmo::Draw(SceneDocument& doc, const EditorSelection& selection, const RenderView& view, float x,
	    float y, float w, float h, std::string& error, const ViewportSettings& settings) {
		Scene* scene = doc.Resolve();
		const auto id = selection.Resolve(doc);
		if (m_Operation < 0 || !scene || !id.HasValue() ||
		    (!view.independentCamera && view.camera.scene != scene->GetWorld().GetIdentity())) {
			Cancel(error);
			return false;
		}
		const auto ref = scene->MakeReference(id);
		if (m_Drag.Active() &&
		    (!m_Drag.Matches(doc, ref) || m_TargetGeneration != view.targetGeneration || m_Camera != view.camera ||
		        m_CameraRevision != view.historyRevision)) {
			if (!Cancel(error))
				return true;
		}
		TransformComponent transform;
		RelationshipComponent relation;
		if (!scene->GetTransform(id, transform).IsSuccess() || !transform.worldValid ||
		    !scene->GetRelationship(id, relation).IsSuccess()) {
			Cancel(error);
			return false;
		}
		Math::Mat4 parent{1};
		if (relation.parent.HasValue()) {
			TransformComponent p;
			if (!scene->GetTransform(relation.parent, p).IsSuccess() || !p.worldValid) {
				Cancel(error);
				return false;
			}
			parent = p.worldMatrix;
		}
		if (m_Drag.Active() && parent != m_ParentWorld) {
			Cancel(error);
			m_WaitRelease = true;
			error = "Parent moved during manipulation";
			return false;
		}
		const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (escape) {
			Cancel(error);
			m_WaitRelease = true;
			ImGuizmo::Enable(false);
		}
		if (m_WaitRelease && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			ImGuizmo::Enable(false);
			return true;
		}
		m_WaitRelease = false;
		ImGuizmo::Enable(true);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetOrthographic(view.lens.projection == ProjectionType::Orthographic);
		ImGuizmo::SetRect(x, y, w, h);
		/* ImGuizmo 按屏幕上方为 +Y；引擎只在 Vulkan 投影内翻 Y，适配局部副本。 */
		auto projection = view.matrices.projection;
		if (view.viewport.flipY)
			for (int col = 0; col < 4; ++col)
				projection[col][1] = -projection[col][1];
		if (!m_Drag.Active())
			m_Preview = transform.worldMatrix;
		const auto op =
		    m_Operation == 0 ? ImGuizmo::TRANSLATE : (m_Operation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
		/* 位移按轴输入米，旋转按角度输入度。缩放倍率由下方按拖动起点吸附。 */
		const float increment = op == ImGuizmo::ROTATE ? settings.rotationStep : settings.translationStep;
		const float step[3]{increment, increment, increment};
		const bool changed = ImGuizmo::Manipulate(&view.matrices.view[0][0], &projection[0][0], op,
		    m_Mode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &m_Preview[0][0], nullptr,
		    settings.snapEnabled && op != ImGuizmo::SCALE ? step : nullptr);
		const bool usingGizmo = ImGuizmo::IsUsing();
		if (usingGizmo || (m_Drag.Active() && changed)) {
			if (!m_Drag.Active()) {
				if (!m_Drag.Begin(doc, ref, error))
					return true;
				m_ParentWorld = parent;
				m_StartWorld = transform.worldMatrix;
				m_StartLocalScale = transform.local.scale;
				m_TargetGeneration = view.targetGeneration;
				m_Camera = view.camera;
				m_CameraRevision = view.historyRevision;
				if (settings.snapEnabled && op == ImGuizmo::ROTATE) {
					// 拾取旋转环时，夹角计算的浮点残差不能计入首个吸附增量。
					m_Preview = m_StartWorld;
					return true;
				}
			}
			if (settings.snapEnabled && op == ImGuizmo::ROTATE && !changed)
				return true;
			Math::TransformTRS next = transform.local;
			Math::MathStatus status;
			if (settings.snapEnabled && op == ImGuizmo::SCALE) {
				status =
				    SnapGizmoScale(m_StartLocalScale, m_StartWorld, m_Preview, settings.scaleStepPercent, next.scale);
			} else {
				Math::Mat4 inverse, local;
				status = Math::InverseAffine(parent, inverse);
				if (status.IsSuccess())
					status = Math::ComposeWorld(inverse, m_Preview, local);
				if (status.IsSuccess())
					status = Math::DecomposeTRS(local, next);
			}
			if (!status.IsSuccess() || !m_Drag.Preview(next, error)) {
				Cancel(error);
				m_WaitRelease = true;
				if (!status.IsSuccess())
					error = status.message;
			} else if (!usingGizmo)
				m_Drag.Finish(doc, error);
			return true;
		}
		if (m_Drag.Active()) {
			m_Drag.Finish(doc, error);
			return true;
		}
		return ImGuizmo::IsOver();
	}
}
