#include "Viewport/TransformGizmo.h"
#include "Commands/SceneCommands.h"
#include "imgui.h"
#include "ImGuizmo.h"

namespace Catalpa::Editor {
    void TransformGizmo::Toolbar(const PanelText& text) {
        ImGui::Combo(text("gizmo.operation", "Tool", "GizmoOperation").c_str(), &m_Operation,
                     "Translate\0Rotate\0Scale\0");
        ImGui::Combo(text("gizmo.space", "Space", "GizmoSpace").c_str(), &m_Mode, "Local\0World\0");
        ImGui::Checkbox(text("gizmo.snap", "Snap", "GizmoSnap").c_str(), &m_Snap);
        if (m_Snap) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("##SnapValue", &m_Step);
        }
    }

    bool TransformGizmo::Draw(SceneDocument& doc, const EditorSelection& selection, const RenderView& view,
                              float x, float y, float w, float h, std::string& error) {
        Scene* scene = doc.Resolve();
        const auto id = selection.Resolve(doc);
        if (!scene || !id.HasValue() || view.camera.scene != scene->GetWorld().GetIdentity()) {
            Cancel(error);
            return false;
        }
        const auto ref = scene->MakeReference(id);
        if (m_Drag.Active() && (!m_Drag.Matches(doc, ref) || m_TargetGeneration != view.targetGeneration ||
                                m_Camera != view.camera || m_CameraRevision != view.historyRevision)) {
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
        // ImGuizmo 按屏幕上方为 +Y；引擎只在 Vulkan 投影内翻 Y，适配局部副本。
        auto projection = view.matrices.projection;
        if (view.viewport.flipY)
            for (int col = 0; col < 4; ++col)
                projection[col][1] = -projection[col][1];
        if (!m_Drag.Active())
            m_Preview = transform.worldMatrix;
        const auto op =
            m_Operation == 0 ? ImGuizmo::TRANSLATE : (m_Operation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
        const float step[3]{m_Step, m_Step, m_Step};
        ImGuizmo::Manipulate(&view.matrices.view[0][0], &projection[0][0], op,
                             m_Mode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &m_Preview[0][0], nullptr,
                             m_Snap && std::isfinite(m_Step) && m_Step > 0 ? step : nullptr);
        if (ImGuizmo::IsUsing()) {
            if (!m_Drag.Active()) {
                if (!m_Drag.Begin(doc, ref, error))
                    return true;
                m_ParentWorld = parent;
                m_TargetGeneration = view.targetGeneration;
                m_Camera = view.camera;
                m_CameraRevision = view.historyRevision;
            }
            Math::Mat4 inverse, local;
            Math::TransformTRS next;
            auto status = Math::InverseAffine(parent, inverse);
            if (status.IsSuccess())
                status = Math::ComposeWorld(inverse, m_Preview, local);
            if (status.IsSuccess())
                status = Math::DecomposeTRS(local, next);
            if (!status.IsSuccess() || !m_Drag.Preview(next, error)) {
                Cancel(error);
                m_WaitRelease = true;
                if (!status.IsSuccess())
                    error = status.message;
            }
            return true;
        }
        if (m_Drag.Active()) {
            m_Drag.Finish(doc, error);
            return true;
        }
        return ImGuizmo::IsOver();
    }
}
