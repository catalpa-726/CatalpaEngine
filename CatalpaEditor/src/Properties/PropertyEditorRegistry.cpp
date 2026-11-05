#include "Properties/PropertyEditorRegistry.h"
#include "Commands/SceneCommands.h"
#include "ECS/ComponentRegistry.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        // 每个数值字段在 Enter 确认后生成一条命令；草稿未确认时不改 World。
        constexpr auto CommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

        void DrawCamera(SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error) {
            CameraComponent camera;
            if (!doc.Resolve()->GetWorld().Get(ref.resolved, camera).IsSuccess())
                return;
            ImGui::SeparatorText(text("edit.camera", "Camera", nullptr).c_str());
            ImGui::TextDisabled("%s: %llu / %s: %llu", text("edit.view_id", "View ID", nullptr).c_str(),
                                static_cast<unsigned long long>(camera.viewId),
                                text("edit.history_revision", "History revision", nullptr).c_str(),
                                static_cast<unsigned long long>(camera.historyRevision));
            bool changed =
                ImGui::Checkbox(text("edit.enabled", "Enabled", "CameraEnabled").c_str(), &camera.enabled);
            changed |= ImGui::InputScalar(text("edit.camera_priority", "Priority", "CameraPriority").c_str(),
                                          ImGuiDataType_S32, &camera.priority, nullptr, nullptr, nullptr,
                                          CommitOnEnter);
            auto& lens = camera.lens;
            int projection = static_cast<int>(lens.projection);
            if (ImGui::Combo(text("edit.projection", "Projection", "CameraProjection").c_str(), &projection,
                             "Perspective\0Orthographic\0")) {
                lens.projection = static_cast<ProjectionType>(projection);
                changed = true;
            }
            if (lens.projection == ProjectionType::Perspective)
                changed |=
                    ImGui::InputFloat(text("edit.vertical_fov", "Vertical FOV (rad)", "VerticalFov").c_str(),
                                      &lens.verticalFovRadians, 0, 0, "%.5g", CommitOnEnter);
            else
                changed |= ImGui::InputFloat(
                    text("edit.ortho_height", "Orthographic height (m)", "OrthoHeight").c_str(),
                    &lens.orthographicHeight, 0, 0, "%.5g", CommitOnEnter);
            changed |= ImGui::InputFloat(text("edit.near_plane", "Near plane (m)", "NearPlane").c_str(),
                                         &lens.nearPlane, 0, 0, "%.5g", CommitOnEnter);
            if (!lens.infiniteFar)
                changed |= ImGui::InputFloat(text("edit.far_plane", "Far plane (m)", "FarPlane").c_str(),
                                             &lens.farPlane, 0, 0, "%.5g", CommitOnEnter);
            if (lens.projection == ProjectionType::Perspective)
                changed |=
                    ImGui::Checkbox(text("edit.infinite_far", "Infinite far plane", "InfiniteFar").c_str(),
                                    &lens.infiniteFar);
            changed |=
                ImGui::Checkbox(text("edit.reversed_z", "Reversed Z", "ReversedZ").c_str(), &lens.reversedZ);
            if (changed)
                EditCamera(doc, ref.persistent, camera, error);
        }

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
            const char* kinds[] = {"Directional", "Point", "Spot"};
            int type = static_cast<int>(p.type);
            ImGui::SeparatorText(text("edit.light", "Light", nullptr).c_str());
            bool changed =
                ImGui::Checkbox(text("edit.enabled", "Enabled", "LightEnabled").c_str(), &light.enabled);
            if (ImGui::Combo(text("edit.light_type", "Type", "LightType").c_str(), &type, kinds, 3)) {
                p.type = static_cast<LightType>(type);
                changed = true;
            }
            changed |= ImGui::InputFloat3(text("edit.color", "Linear color", "LightColor").c_str(),
                                          &p.color.x, "%.4g", CommitOnEnter);
            changed |= ImGui::InputFloat(
                text("edit.light_intensity", "Intensity (lux for sun, cd otherwise)", "LightIntensity")
                    .c_str(),
                &p.intensity, 0, 0, "%.5g", CommitOnEnter);
            if (p.type != LightType::Directional)
                changed |= ImGui::InputFloat(text("edit.range", "Range (m)", "Range").c_str(), &p.range, 0, 0,
                                             "%.5g", CommitOnEnter);
            if (p.type == LightType::Spot) {
                changed |= ImGui::InputFloat(text("edit.inner_cone", "Inner cone (rad)", "InnerCone").c_str(),
                                             &p.innerConeRadians, 0, 0, "%.5g", CommitOnEnter);
                changed |= ImGui::InputFloat(text("edit.outer_cone", "Outer cone (rad)", "OuterCone").c_str(),
                                             &p.outerConeRadians, 0, 0, "%.5g", CommitOnEnter);
            }
            changed |= ImGui::Checkbox(text("edit.cast_shadows", "Cast shadows", "CastShadows").c_str(),
                                       &p.castShadows);
            changed |= ImGui::InputFloat(text("edit.bias", "Shadow bias (m)", "ShadowBias").c_str(),
                                         &p.shadowBiasMeters, 0, 0, "%.5g", CommitOnEnter);
            if (changed)
                EditLight(doc, ref.persistent, light, error);
        }

        void DrawEnvironment(SceneDocument& doc, EntityReference ref, const PanelText& text,
                             std::string& error) {
            EnvironmentComponent env;
            if (!doc.Resolve()->GetWorld().Get(ref.resolved, env).IsSuccess())
                return;
            ImGui::SeparatorText(text("edit.environment", "Environment", nullptr).c_str());
            ImGui::TextUnformatted(env.environment.id.ToString().c_str());
            auto& p = env.settings;
            bool changed =
                ImGui::Checkbox(text("edit.enabled", "Enabled", "EnvEnabled").c_str(), &env.enabled);
            changed |= ImGui::InputFloat(text("edit.strength", "Intensity", "EnvIntensity").c_str(),
                                         &p.intensity, 0, 0, "%.5g", CommitOnEnter);
            changed |= ImGui::InputFloat(text("edit.angle", "Rotation (rad)", "EnvRotation").c_str(),
                                         &p.rotationRadians, 0, 0, "%.5g", CommitOnEnter);
            changed |= ImGui::InputFloat(text("edit.exposure", "Exposure (EV)", "EnvExposure").c_str(),
                                         &p.exposureEV, 0, 0, "%.5g", CommitOnEnter);
            changed |= ImGui::Checkbox(text("edit.background", "Draw background", "EnvBackground").c_str(),
                                       &p.drawBackground);
            changed |= ImGui::Checkbox(text("edit.fog_enabled", "Distance fog", "FogEnabled").c_str(),
                                       &p.fogEnabled);
            if (p.fogEnabled) {
                changed |=
                    ImGui::InputFloat3(text("edit.fog_color", "Fog color (linear RGB)", "FogColor").c_str(),
                                       &p.fogColor.x, "%.5g", CommitOnEnter);
                changed |=
                    ImGui::InputFloat(text("edit.fog_density", "Fog density (1/m)", "FogDensity").c_str(),
                                      &p.fogDensityPerMeter, 0, 0, "%.5g", CommitOnEnter);
            }
            if (changed)
                EditEnvironment(doc, ref.persistent, env, error);
        }
    }

    bool PropertyEditorRegistry::DrawTransform(SceneDocument& doc, EntityReference reference,
                                               std::string& error) const {
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
        bool changed = false;
        for (const auto& field : schema->fields) {
            auto candidate = value.local;
            auto flags = ImGuiInputTextFlags_EnterReturnsTrue;
            ImGui::PushID(field.id.c_str());
            bool submit = false;
            if (field.id == "translation" && field.kind == ComponentFieldKind::Float3)
                submit =
                    ImGui::InputFloat3(field.displayName.c_str(), &candidate.translation.x, "%.5g", flags);
            else if (field.id == "scale" && field.kind == ComponentFieldKind::Float3)
                submit = ImGui::InputFloat3(field.displayName.c_str(), &candidate.scale.x, "%.5g", flags);
            else if (field.id == "rotation" && field.kind == ComponentFieldKind::Float4) {
                float wxyz[]{candidate.rotation.w, candidate.rotation.x, candidate.rotation.y,
                             candidate.rotation.z};
                submit = ImGui::InputFloat4(field.displayName.c_str(), wxyz, "%.6g", flags);
                candidate.rotation = {wxyz[0], wxyz[1], wxyz[2], wxyz[3]};
            }
            ImGui::PopID();
            if (submit && ChangeLocal(doc, reference.persistent, value.local, candidate, error)) {
                value.local = candidate;
                changed = true;
            }
        }
        return changed;
    }

    void PropertyEditorRegistry::Draw(SceneDocument& doc, EntityReference ref, const PanelText& text,
                                      std::string& error) const {
        auto* scene = doc.Resolve();
        const auto* schemas = scene ? scene->GetComponentSchemas() : nullptr;
        if (!schemas)
            return;
        // 适配器使用稳定 schema 类型选择对应值控件，缺失 schema 或未知组件继续只读。
        if (schemas->Find("catalpa.transform"))
            DrawTransform(doc, ref, error);
        if (schemas->Find("catalpa.mesh_renderer")) {
            MeshRendererComponent mesh;
            if (doc.Resolve()->GetWorld().Get(ref.resolved, mesh).IsSuccess()) {
                ImGui::SeparatorText(text("edit.mesh", "Mesh renderer", nullptr).c_str());
                bool changed = ImGui::InputFloat4(text("edit.color", "Linear color", "MeshColor").c_str(),
                                                  &mesh.color.x, "%.4g", CommitOnEnter);
                changed |=
                    ImGui::Checkbox(text("edit.visible", "Visible", "MeshVisible").c_str(), &mesh.visible);
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
