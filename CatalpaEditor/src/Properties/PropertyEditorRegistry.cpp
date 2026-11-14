#include "Properties/PropertyEditorRegistry.h"

#include "Commands/SceneCommands.h"
#include "ECS/ComponentRegistry.h"
#include "imgui.h"
#include "Panels/EditorText.h"

namespace Catalpa::Editor {
	namespace {
		/* 每个数值字段在 Enter 确认后生成一条命令；草稿未确认时不改 World。 */
		constexpr auto CommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

		bool BeginProperties(const PanelText& text, const char* key, const char* title, const char* id) {
			if (!ImGui::CollapsingHeader(text(key, title, id).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				return false;
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
				return false;
			ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch, .32f);
			ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, .68f);
			return true;
		}

		void Field(std::string_view label) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label.data(), label.data() + label.size());
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
		}

		std::string Field(const PanelText& text, const char* key, const char* fallback, const char* id) {
			Field(text(key, fallback, nullptr));
			return std::string("##") + id;
		}

		template <class Number> bool AxisInputs(Number* values, ImGuiDataType type, const char* format) {
			const ImU32 colors[]{
			    IM_COL32(231, 104, 102, 255), IM_COL32(117, 200, 141, 255), IM_COL32(112, 166, 232, 255)};
			constexpr const char* names[]{"X", "Y", "Z"};
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float width = std::max(1.f, (ImGui::GetContentRegionAvail().x - 2 * spacing) / 3.f);
			bool committed = false;
			for (int axis = 0; axis < 3; ++axis) {
				if (axis)
					ImGui::SameLine();
				ImGui::PushID(axis);
				ImGui::SetNextItemWidth(width);
				committed |= ImGui::InputScalar("##Axis", type, &values[axis], nullptr, nullptr, format, CommitOnEnter);
				const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddLine({a.x + 1, b.y - 1}, {b.x - 1, b.y - 1}, colors[axis], 2);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", names[axis]);
				ImGui::PopID();
			}
			return committed;
		}

		/**
		 * @brief 绘制相机。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] ref 本次操作使用的 EntityReference。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawCamera(SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error) {
			CameraComponent camera;
			if (!doc.Resolve()->GetWorld().Get(ref.resolved, camera).IsSuccess())
				return;
			if (!BeginProperties(text, "edit.camera", "Camera", "CameraSection"))
				return;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s: %llu / %s: %llu", text("edit.view_id", "View ID", nullptr).c_str(),
			    static_cast<unsigned long long>(camera.viewId),
			    text("edit.history_revision", "History revision", nullptr).c_str(),
			    static_cast<unsigned long long>(camera.historyRevision));
			bool changed =
			    ImGui::Checkbox(Field(text, "edit.enabled", "Enabled", "CameraEnabled").c_str(), &camera.enabled);
			changed |= ImGui::InputScalar(Field(text, "edit.camera_priority", "Priority", "CameraPriority").c_str(),
			    ImGuiDataType_S32, &camera.priority, nullptr, nullptr, nullptr, CommitOnEnter);
			auto& lens = camera.lens;
			int projection = static_cast<int>(lens.projection);
			const char* projections[]{
			    UiText("preview.perspective", "Perspective"), UiText("preview.ortho", "Orthographic")};
			if (ImGui::Combo(Field(text, "edit.projection", "Projection", "CameraProjection").c_str(), &projection,
			        projections, 2)) {
				lens.projection = static_cast<ProjectionType>(projection);
				changed = true;
			}
			if (lens.projection == ProjectionType::Perspective)
				changed |=
				    ImGui::InputFloat(Field(text, "edit.vertical_fov", "Vertical FOV (rad)", "VerticalFov").c_str(),
				        &lens.verticalFovRadians, 0, 0, "%.5g", CommitOnEnter);
			else
				changed |= ImGui::InputFloat(
				    Field(text, "edit.ortho_height", "Orthographic height (m)", "OrthoHeight").c_str(),
				    &lens.orthographicHeight, 0, 0, "%.5g", CommitOnEnter);
			changed |= ImGui::InputFloat(Field(text, "edit.near_plane", "Near plane (m)", "NearPlane").c_str(),
			    &lens.nearPlane, 0, 0, "%.5g", CommitOnEnter);
			if (!lens.infiniteFar)
				changed |= ImGui::InputFloat(Field(text, "edit.far_plane", "Far plane (m)", "FarPlane").c_str(),
				    &lens.farPlane, 0, 0, "%.5g", CommitOnEnter);
			if (lens.projection == ProjectionType::Perspective)
				changed |= ImGui::Checkbox(
				    Field(text, "edit.infinite_far", "Infinite far plane", "InfiniteFar").c_str(), &lens.infiniteFar);
			changed |=
			    ImGui::Checkbox(Field(text, "edit.reversed_z", "Reversed Z", "ReversedZ").c_str(), &lens.reversedZ);
			ImGui::EndTable();
			if (changed)
				EditCamera(doc, ref.persistent, camera, error);
		}

		/**
		 * @brief 绘制光源。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] ref 本次操作使用的 EntityReference。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawLight(SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error) {
			LightComponent light;
			if (!doc.Resolve()->GetWorld().Has<LightComponent>(ref.resolved)) {
				if (ImGui::Button(text("edit.add_light", "Add light", "AddLight").c_str()))
					EditLight(doc, ref.persistent, light, error);
				return;
			}
			if (!doc.Resolve()->GetWorld().Get(ref.resolved, light).IsSuccess())
				return;
			auto& p = light.parameters;
			const char* kinds[] = {UiText("light.directional", "Directional"), UiText("light.point", "Point"),
			    UiText("light.spot", "Spot")};
			int type = static_cast<int>(p.type);
			if (!BeginProperties(text, "edit.light", "Light", "LightSection"))
				return;
			bool changed =
			    ImGui::Checkbox(Field(text, "edit.enabled", "Enabled", "LightEnabled").c_str(), &light.enabled);
			if (ImGui::Combo(Field(text, "edit.light_type", "Type", "LightType").c_str(), &type, kinds, 3)) {
				p.type = static_cast<LightType>(type);
				changed = true;
			}
			changed |= ImGui::InputFloat3(
			    Field(text, "edit.color", "Linear color", "LightColor").c_str(), &p.color.x, "%.4g", CommitOnEnter);
			changed |= ImGui::InputFloat(
			    Field(text, "edit.light_intensity", "Intensity (lux for sun, cd otherwise)", "LightIntensity").c_str(),
			    &p.intensity, 0, 0, "%.5g", CommitOnEnter);
			if (p.type != LightType::Directional)
				changed |= ImGui::InputFloat(
				    Field(text, "edit.range", "Range (m)", "Range").c_str(), &p.range, 0, 0, "%.5g", CommitOnEnter);
			if (p.type == LightType::Spot) {
				changed |= ImGui::InputFloat(Field(text, "edit.inner_cone", "Inner cone (rad)", "InnerCone").c_str(),
				    &p.innerConeRadians, 0, 0, "%.5g", CommitOnEnter);
				changed |= ImGui::InputFloat(Field(text, "edit.outer_cone", "Outer cone (rad)", "OuterCone").c_str(),
				    &p.outerConeRadians, 0, 0, "%.5g", CommitOnEnter);
			}
			changed |= ImGui::Checkbox(
			    Field(text, "edit.cast_shadows", "Cast shadows", "CastShadows").c_str(), &p.castShadows);
			changed |= ImGui::InputFloat(Field(text, "edit.bias", "Shadow bias (m)", "ShadowBias").c_str(),
			    &p.shadowBiasMeters, 0, 0, "%.5g", CommitOnEnter);
			ImGui::EndTable();
			if (changed)
				EditLight(doc, ref.persistent, light, error);
		}

		/**
		 * @brief 绘制环境。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] ref 本次操作使用的 EntityReference。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawEnvironment(SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error) {
			EnvironmentComponent env;
			if (!doc.Resolve()->GetWorld().Get(ref.resolved, env).IsSuccess())
				return;
			if (!BeginProperties(text, "edit.environment", "Environment", "EnvironmentSection"))
				return;
			Field(text("edit.environment_asset", "Environment asset", nullptr));
			ImGui::TextUnformatted(env.environment.id.ToString().c_str());
			auto& p = env.settings;
			bool changed = ImGui::Checkbox(Field(text, "edit.enabled", "Enabled", "EnvEnabled").c_str(), &env.enabled);
			changed |= ImGui::InputFloat(Field(text, "edit.strength", "Intensity", "EnvIntensity").c_str(),
			    &p.intensity, 0, 0, "%.5g", CommitOnEnter);
			changed |= ImGui::InputFloat(Field(text, "edit.angle", "Rotation (rad)", "EnvRotation").c_str(),
			    &p.rotationRadians, 0, 0, "%.5g", CommitOnEnter);
			changed |= ImGui::InputFloat(Field(text, "edit.exposure", "Exposure (EV)", "EnvExposure").c_str(),
			    &p.exposureEV, 0, 0, "%.5g", CommitOnEnter);
			changed |= ImGui::Checkbox(
			    Field(text, "edit.background", "Draw background", "EnvBackground").c_str(), &p.drawBackground);
			changed |=
			    ImGui::Checkbox(Field(text, "edit.fog_enabled", "Distance fog", "FogEnabled").c_str(), &p.fogEnabled);
			if (p.fogEnabled) {
				changed |=
				    ImGui::InputFloat3(Field(text, "edit.fog_color", "Fog color (linear RGB)", "FogColor").c_str(),
				        &p.fogColor.x, "%.5g", CommitOnEnter);
				changed |= ImGui::InputFloat(Field(text, "edit.fog_density", "Fog density (1/m)", "FogDensity").c_str(),
				    &p.fogDensityPerMeter, 0, 0, "%.5g", CommitOnEnter);
			}
			ImGui::EndTable();
			if (changed)
				EditEnvironment(doc, ref.persistent, env, error);
		}
	}

	/**
	 * @brief 绘制变换。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] reference 在当前场景文档中定位要编辑变换的实体引用。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool PropertyEditorRegistry::DrawTransform(
	    SceneDocument& doc, EntityReference reference, const PanelText& text, std::string& error) const {
		Scene* scene = doc.Resolve();
		if (!scene)
			return false;
		const auto entity = scene->Resolve(reference);
		TransformComponent value;
		if (!scene->GetTransform(entity, value).IsSuccess())
			return false;
		const auto* registry = scene->GetComponentSchemas();
		const auto* schema = registry ? registry->Find("catalpa.transform") : nullptr;
		if (!schema)
			return false;
		if (!BeginProperties(text, "edit.transform", "Transform", "TransformSection"))
			return false;
		bool changed = false;
		for (const auto& field : schema->fields) {
			auto candidate = value.local;
			ImGui::PushID(field.id.c_str());
			bool submit = false;
			if (field.id == "translation" && field.kind == ComponentFieldKind::Float3) {
				Field(text("edit.position_m", "Position (m)", nullptr));
				submit = AxisInputs(&candidate.translation.x, ImGuiDataType_Float, "%.5g");
			} else if (field.id == "scale" && field.kind == ComponentFieldKind::Float3) {
				Field(text("edit.scale", "Scale", nullptr));
				submit = AxisInputs(&candidate.scale.x, ImGuiDataType_Float, "%.5g");
			} else if (field.id == "rotation" && field.kind == ComponentFieldKind::Float4) {
				Field(text("edit.rotation_degrees", "Rotation XYZ (deg)", nullptr));
				if (m_Rotation.Synchronize(doc.Id(), reference, candidate.rotation, error)) {
					ImGui::PushID(std::to_string(m_Rotation.Revision()).c_str());
					submit = AxisInputs(m_Rotation.Degrees().data(), ImGuiDataType_Double, "%.4f");
					ImGui::PopID();
					if (submit)
						submit = m_Rotation.ToQuaternion(candidate.rotation, error) &&
						    candidate.rotation != value.local.rotation;
				}
			}
			ImGui::PopID();
			if (submit && ChangeLocal(doc, reference.persistent, value.local, candidate, error)) {
				value.local = candidate;
				if (field.id == "rotation")
					m_Rotation.Accept(candidate.rotation);
				changed = true;
			}
		}
		ImGui::EndTable();
		return changed;
	}

	void PropertyEditorRegistry::Draw(SceneDocument& doc, EntityReference ref, const PanelText& text,
	    AssetVersionRef<MaterialInstance>& openMaterial, std::string& error) const {
		auto* scene = doc.Resolve();
		const auto* schemas = scene ? scene->GetComponentSchemas() : nullptr;
		if (!schemas)
			return;
		/* 适配器按模式中的稳定类型选择值控件。缺少模式或遇到未知组件时，只允许读取。 */
		if (schemas->Find("catalpa.transform"))
			DrawTransform(doc, ref, text, error);
		if (schemas->Find("catalpa.mesh_renderer")) {
			MeshRendererComponent mesh;
			if (doc.Resolve()->GetWorld().Get(ref.resolved, mesh).IsSuccess() &&
			    BeginProperties(text, "edit.mesh", "Mesh renderer", "MeshSection")) {
				bool changed = ImGui::InputFloat4(Field(text, "edit.color", "Linear color", "MeshColor").c_str(),
				    &mesh.color.x, "%.4g", CommitOnEnter);
				changed |=
				    ImGui::Checkbox(Field(text, "edit.visible", "Visible", "MeshVisible").c_str(), &mesh.visible);
				for (size_t i = 0; i < mesh.materials.size(); ++i) {
					const auto& material = mesh.materials[i];
					const auto* layout = mesh.meshVersion.HasValue() ? mesh.meshVersion.Get() : nullptr;
					Field(layout && i < layout->materialSlots.size()
					        ? layout->materialSlots[i]
					        : text("edit.material_slot", "Material slot", nullptr));
					ImGui::PushID(int(i));
					if (material.HasValue()) {
						const auto* definition = material.Get()->definition.Get();
						if (definition)
							ImGui::TextUnformatted(definition->family.c_str());
						if (ImGui::Button(text("edit.open_material", "Edit material", "EditMaterial").c_str()))
							openMaterial = material;
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", material.GetId().ToString().c_str());
					} else
						ImGui::TextDisabled("%s", text("edit.unassigned", "Unassigned", nullptr).c_str());
					ImGui::PopID();
				}
				ImGui::EndTable();
				if (changed)
					EditMeshDisplay(doc, ref.persistent, mesh.color, mesh.visible, error);
			}
		}
		if (schemas->Find("catalpa.camera"))
			DrawCamera(doc, ref, text, error);
		if (schemas->Find("catalpa.light"))
			DrawLight(doc, ref, text, error);
		if (schemas->Find("catalpa.environment"))
			DrawEnvironment(doc, ref, text, error);
	}
}
