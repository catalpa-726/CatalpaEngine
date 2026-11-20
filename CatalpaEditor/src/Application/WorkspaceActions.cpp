#include "catpch.h"
#include "Application/WorkspaceEditing.h"
#include "Commands/SceneCommands.h"
#include "Documents/DocumentRecovery.h"
#include "Core/Application.h"
#include "Runtime/Systems/TransformSystem.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "Viewport/ViewportPlacement.h"

namespace Catalpa::Editor {
	bool WorkspaceEditing::CanEditAction(EditAction action) {
		auto* doc = m_Documents.Active();
		if (!doc || m_PlayDocument.HasValue())
			return false;
		if (action == EditAction::Save)
			return !DocumentRecovery::AuthorSuffix(doc->TypeKey()).empty();
		if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput)
			return false;
		if (action == EditAction::Undo)
			return doc->History().CanUndo();
		if (action == EditAction::Redo)
			return doc->History().CanRedo();
		auto* scene = dynamic_cast<SceneDocument*>(doc);
		if (!scene || !scene->Resolve() || scene->Playing())
			return false;
		if (action == EditAction::Paste)
			return m_Clipboard.has_value() && m_ClipboardDocument == doc->Id();
		return m_Selection.Resolve(*scene).HasValue();
	}

	void WorkspaceEditing::FrameSelection() {
		auto* doc = dynamic_cast<SceneDocument*>(m_Documents.Active());
		auto* scene = doc ? doc->Resolve() : nullptr;
		if (!scene)
			return;
		if (auto status = TransformSystem::Update(*scene); !status.IsSuccess()) {
			m_Error = status.message;
			return;
		}
		Aabb bounds;
		if (!SelectionBounds(*scene, m_Selection.Resolve(*doc), bounds, m_Error))
			return;
		const auto center = (bounds.minimum + bounds.maximum) * .5f;
		const float radius = std::max(glm::length(bounds.maximum - center), 1.f);
		RenderSurfaceSnapshot frame;
		CameraViewport viewport{640, 480};
		if (m_App.GetEditorPreview(0x43415453434e4501ULL, frame))
			viewport = frame.view.viewport;
		if (m_Camera.Frame(center, radius, m_Error, viewport)) {
			m_ResetCamera = false;
			m_GameView = false;
		}
	}

	void WorkspaceEditing::PerformEditAction(EditAction action) {
		if (!CanEditAction(action))
			return;
		auto& doc = *m_Documents.Active();
		if (action == EditAction::Undo) {
			(void)doc.History().Undo(doc, m_Error);
			return;
		}
		if (action == EditAction::Redo) {
			(void)doc.History().Redo(doc, m_Error);
			return;
		}
		if (action == EditAction::Save) {
			(void)SaveDocument(doc);
			return;
		}
		auto& scene = *dynamic_cast<SceneDocument*>(&doc);
		const auto selected = m_Selection.Resolve(scene);
		const auto reference = scene.Resolve()->MakeReference(selected);
		if (action == EditAction::Copy) {
			SceneSubtreeSnapshot snapshot;
			const auto result = scene.Resolve()->CaptureForUndo(selected, snapshot);
			if (!result.IsSuccess()) {
				m_Error = result.message;
				return;
			}
			m_Clipboard = std::move(snapshot);
			m_ClipboardDocument = doc.Id();
		} else if (action == EditAction::Paste) {
			PersistentEntityId root;
			if (PasteAuthorSubtree(scene, *m_Clipboard, root, m_Error)) {
				m_Selection.Select(scene, scene.Resolve()->Resolve(root));
				m_Picking.Cancel(m_App);
			}
		} else if (action == EditAction::Delete) {
			if (DeleteAuthorEntity(scene, reference.persistent, m_Error)) {
				m_Selection.Clear();
				m_Picking.Cancel(m_App);
			}
		} else if (action == EditAction::Rename) {
			m_RevealHierarchy = true;
			m_Hierarchy.RequestRename();
		} else if (action == EditAction::Frame)
			FrameSelection();
	}

	void WorkspaceEditing::HandleShortcuts() {
		const auto& io = ImGui::GetIO();
		/* 保存不属于文本缓冲区操作；源代码输入保持焦点时也能保存当前作者文档。 */
		if (!io.AppFocusLost && io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift &&
		    !m_PlayDocument.HasValue() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
		    ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			PerformEditAction(EditAction::Save);
			return;
		}
		/* 文本控件使用自己的剪贴板与撤销栈；鼠标操作和弹窗期间不触发场景命令。 */
		if (io.AppFocusLost || io.WantTextInput || ImGui::IsAnyItemActive() ||
		    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || io.KeyAlt || io.KeySuper || m_RightLook ||
		    m_PlayDocument.HasValue())
			return;
		bool entityFocus = false;
		for (auto* focused = ImGui::GetCurrentContext()->NavWindow; focused; focused = focused->ParentWindow) {
			if (focused->ID == ImHashStr("###Catalpa.Scene") || focused->ID == ImHashStr("###Catalpa.Viewport") ||
			    focused->ID == ImHashStr("###Catalpa.Inspector")) {
				entityFocus = true;
				break;
			}
		}

		if (io.KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
				PerformEditAction(io.KeyShift ? EditAction::Redo : EditAction::Undo);
			else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
				PerformEditAction(EditAction::Redo);
			else if (entityFocus && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false))
				PerformEditAction(EditAction::Copy);
			else if (entityFocus && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false))
				PerformEditAction(EditAction::Paste);
		} else if (entityFocus && !io.KeyShift) {
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				PerformEditAction(EditAction::Delete);
			else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
				PerformEditAction(EditAction::Rename);
			else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
				PerformEditAction(EditAction::Frame);
		}
	}
}
