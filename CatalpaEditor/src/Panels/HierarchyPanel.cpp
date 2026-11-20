#include "Panels/HierarchyPanel.h"

#include "Commands/SceneCommands.h"
#include "Commands/PrefabCommands.h"
#include "Assets/BuiltinAssetIds.h"
#include "Panels/FormLayout.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Catalpa::Editor {
	namespace {
		enum class Icon { Entity, Camera, Light, Mesh };

		void DrawIcon(Icon icon, ImVec2 p, float size) {
			auto* draw = ImGui::GetWindowDrawList();
			const auto color = ImGui::GetColorU32(ImGuiCol_Text);
			const auto at = [=](float x, float y) { return ImVec2(p.x + x * size, p.y + y * size); };
			if (icon == Icon::Light) {
				draw->AddCircle(at(.5f, .5f), size * .20f, color, 12);
				for (int i = 0; i < 8; ++i) {
					const float a = i * .78539816f, x = std::cos(a), y = std::sin(a);
					draw->AddLine(at(.5f + .30f * x, .5f + .30f * y), at(.5f + .48f * x, .5f + .48f * y), color);
				}
			} else if (icon == Icon::Camera) {
				draw->AddRect(at(.05f, .24f), at(.62f, .76f), color);
				draw->AddTriangle(at(.63f, .49f), at(.95f, .25f), at(.95f, .75f), color);
			} else if (icon == Icon::Mesh) {
				const ImVec2 hull[]{
				    at(.5f, .04f), at(.94f, .26f), at(.94f, .73f), at(.5f, .96f), at(.06f, .73f), at(.06f, .26f)};
				draw->AddPolyline(hull, 6, color, 1.f, ImDrawFlags_Closed);
				draw->AddLine(hull[5], at(.5f, .49f), color);
				draw->AddLine(hull[1], at(.5f, .49f), color);
				draw->AddLine(hull[3], at(.5f, .49f), color);
			} else {
				draw->AddCircle(at(.5f, .5f), size * .29f, color, 4);
			}
		}
	}

	bool HierarchyPanel::Draw(
	    SceneDocument& doc, AssetManager& assets, EditorSelection& selection, const PanelText& text, std::string& error) {
		auto* scene = doc.Resolve();
		if (!scene)
			return false;

		struct Row {
			EntityId entity;
			PersistentEntityId id;
			std::string name;
			Icon icon;
		};

		std::map<uint32_t, std::vector<Row>> children;
		auto collected = scene->GetWorld().ForEach([&](EntityId entity) {
			IdentityComponent id;
			RelationshipComponent relation;
			if (auto s = scene->GetIdentity(entity, id); !s.IsSuccess())
				return s;
			if (auto s = scene->GetRelationship(entity, relation); !s.IsSuccess())
				return s;
			const auto& world = scene->GetWorld();
			const auto icon = world.Has<CameraComponent>(entity) ? Icon::Camera
			    : world.Has<LightComponent>(entity)              ? Icon::Light
			    : world.Has<MeshRendererComponent>(entity)       ? Icon::Mesh
			                                                     : Icon::Entity;
			children[relation.parent.index].push_back({entity, id.persistent, std::move(id.name), icon});
			return WorldStatus{};
		});
		if (!collected.IsSuccess()) {
			error = collected.message;
			return false;
		}
		const auto current = selection.Resolve(doc);
		const auto chosen = scene->MakeReference(current);
		bool changed = false;
		std::optional<std::pair<PersistentEntityId, PersistentEntityId>> reparent;
		std::optional<PersistentEntityId> createUnder, remove;
		AssetId primitive;
		std::string primitiveName;
		const auto showShapes = [&](PersistentEntityId parent) {
			using namespace BuiltinAssetIds;
			struct Shape { AssetId mesh; const char* key; const char* name; };
			constexpr Shape shapes[]{{CubeMesh, "primitive.cube", "Cube"}, {SphereMesh, "primitive.sphere", "Sphere"},
			    {CapsuleMesh, "primitive.capsule", "Capsule"}, {CylinderMesh, "primitive.cylinder", "Cylinder"},
			    {PlaneMesh, "primitive.plane", "Plane"}, {QuadMesh, "primitive.quad", "Quad"}};
			for (const auto& shape : shapes) {
				if (ImGui::MenuItem(text(shape.key, shape.name, shape.name).c_str())) {
					primitive = shape.mesh;
					primitiveName = text(shape.key, shape.name, nullptr);
					createUnder = parent;
				}
			}
		};

		struct Drag {
			StableId document;
			EntityReference entity;
		};

		const auto acceptParent = [&](PersistentEntityId parent) {
			if (!ImGui::BeginDragDropTarget())
				return;
			if (const auto* p = ImGui::AcceptDragDropPayload("CAT_ENTITY"); p && p->DataSize == sizeof(Drag)) {
				Drag value;
				std::memcpy(&value, p->Data, sizeof(value));
				if (value.document == doc.Id() && scene->Resolve(value.entity).HasValue())
					reparent.emplace(value.entity.persistent, parent);
			}
			ImGui::EndDragDropTarget();
		};
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##EntitySearch", text("hierarchy.search", "Search entities", nullptr).c_str(),
		    m_Search.data(), m_Search.size());
		if (ImGui::Button(text("hierarchy.create", "Create entity", "NewEntity").c_str())) {
			PersistentEntityId created;
			if (CreateAuthorEntity(doc, "Entity", {}, created, error)) {
				selection.Select(doc, scene->Resolve(created));
				changed = true;
			}
		}
		const auto shapeLabel = text("primitive.menu", "3D Object", "New3DObject");
		const auto fitNextButton = [&](const std::string& label) {
			if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
			        ImGui::CalcTextSize(label.c_str(), nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x <=
			    ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x)
				ImGui::SameLine();
		};
		fitNextButton(shapeLabel);
		if (ImGui::Button(shapeLabel.c_str()))
			ImGui::OpenPopup("##Create3DObject");
		if (ImGui::BeginPopup("##Create3DObject")) {
			showShapes({});
			ImGui::EndPopup();
		}
		const auto deleteLabel = text("hierarchy.delete", "Delete subtree", "DeleteEntity");
		fitNextButton(deleteLabel);
		ImGui::BeginDisabled(!chosen.HasValue());
		if (ImGui::Button(deleteLabel.c_str()))
			remove = chosen.persistent;
		ImGui::EndDisabled();
		ImGui::SeparatorText(text("hierarchy.root", "Scene root", nullptr).c_str());
		acceptParent({});

		/* 显式遍历栈，深层作者层级不会耗尽 UI 调用栈。 */
		struct Pending {
			const Row* row;
			bool close, expand;
		};

		std::vector<Pending> stack;
		auto push = [&](const std::vector<Row>& rows, bool expand = false) {
			for (auto i = rows.rbegin(); i != rows.rend(); ++i)
				stack.push_back({&*i, false, expand});
		};
		const bool searching = m_Search.front() != 0;
		if (searching) {
			for (const auto& [parent, rows] : children)
				for (const auto& row : rows)
					if (ImStristr(row.name.c_str(), nullptr, m_Search.data(), nullptr))
						stack.push_back({&row, false, false});
			std::reverse(stack.begin(), stack.end());
		} else {
			push(children[EntityId{}.index]);
		}
		const auto expandRoot = std::exchange(m_ExpandRoot, {});
		while (!stack.empty()) {
			const auto entry = stack.back();
			stack.pop_back();
			if (entry.close) {
				ImGui::TreePop();
				continue;
			}
			const auto& row = *entry.row;
			const auto child = children.find(row.entity.index);
			const bool leaf = searching || child == children.end();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (leaf)
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (row.entity == current)
				flags |= ImGuiTreeNodeFlags_Selected;
			const float iconSize = ImGui::GetFontSize() * .75f;
			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			const bool expand = entry.expand || (m_ExpandDocument == doc.Id() && expandRoot == row.id);
			if (expand && !leaf)
				ImGui::SetNextItemOpen(true);
			const bool open = ImGui::TreeNodeEx(row.id.ToString().c_str(), flags, "     %s", row.name.c_str());
			DrawIcon(row.icon,
			    ImVec2(cursor.x + ImGui::GetTreeNodeToLabelSpacing(),
			        cursor.y + (ImGui::GetTextLineHeight() - iconSize) * .5f),
			    iconSize);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("%s", row.name.c_str());
			if ((ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ||
			    ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				selection.Select(doc, row.entity);
				changed = true;
			}
			if (ImGui::BeginDragDropSource()) {
				const Drag drag{doc.Id(), scene->MakeReference(row.entity)};
				ImGui::SetDragDropPayload("CAT_ENTITY", &drag, sizeof(drag));
				ImGui::TextUnformatted(row.name.c_str());
				ImGui::EndDragDropSource();
			}
			acceptParent(row.id);
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::BeginMenu(shapeLabel.c_str())) {
					showShapes(row.id);
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem(text("hierarchy.child", "Create child", "NewChild").c_str()))
					createUnder = row.id;
				if (ImGui::MenuItem(text("history.rename", "Rename entity", "RenameEntity").c_str()))
					RequestRename();
				if (ImGui::MenuItem(deleteLabel.c_str()))
					remove = row.id;
				if (ImGui::MenuItem(text("hierarchy.frame", "Frame selection", "FrameSelection").c_str()))
					m_FrameRequested = true;
				if (!leaf &&
				    ImGui::MenuItem(text("hierarchy.expand_subtree", "Expand subtree", "ExpandSubtree").c_str())) {
					m_ExpandDocument = doc.Id();
					m_ExpandRoot = row.id;
				}
				ImGui::EndPopup();
			}
			if (open && !leaf) {
				stack.push_back({nullptr, true, false});
				push(child->second, expand);
			}
		}
		/* 枚举使用的是只读场景快照；结构命令在遍历后提交，并把选择指向实际提交结果。 */
		if (createUnder) {
			PersistentEntityId created;
			if (primitive.HasValue()
			        ? CreatePrimitiveEntity(doc, assets, primitive, primitiveName, *createUnder, created, error)
			        : CreateAuthorEntity(doc, "Entity", *createUnder, created, error)) {
				selection.Select(doc, scene->Resolve(created));
				m_ExpandDocument = doc.Id();
				m_ExpandRoot = *createUnder;
				changed = true;
			}
		}
		if (remove && DeleteAuthorEntity(doc, *remove, error)) {
			selection.Clear();
			changed = true;
		}
		if (reparent && ReparentEntity(doc, reparent->first, reparent->second, error))
			changed = true;
		const auto renameLabel = text("history.rename", "Rename entity", "HierarchyRenameModal");
		if (std::exchange(m_RequestRename, false) && !doc.Playing()) {
			const auto target = scene->MakeReference(selection.Resolve(doc));
			IdentityComponent identity;
			if (target.HasValue() && scene->GetIdentity(target.resolved, identity).IsSuccess() &&
			    identity.name.size() <= 65536) {
				m_RenameDocument = doc.Id();
				m_RenameTarget = target;
				m_OriginalName = identity.name;
				m_RenameBuffer.assign(65537, 0);
				std::copy(m_OriginalName.begin(), m_OriginalName.end(), m_RenameBuffer.begin());
				ImGui::OpenPopup(renameLabel.c_str());
			}
		}
		PrepareEditorModal();
		if (ImGui::BeginPopupModal(renameLabel.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (doc.Id() != m_RenameDocument || !scene->Resolve(m_RenameTarget).HasValue() || doc.Playing()) {
				ImGui::CloseCurrentPopup();
			} else {
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				const bool enter = ImGui::InputText("##EntityRename", m_RenameBuffer.data(), m_RenameBuffer.size(),
				    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (ImGui::Button(text("history.rename", "Rename entity", "ConfirmRename").c_str()) || enter) {
					if (RenameEntity(doc, m_RenameTarget.persistent, m_RenameBuffer.data(), m_OriginalName, error)) {
						ImGui::CloseCurrentPopup();
						changed = true;
					}
				}
				ImGui::SameLine();
				if (ImGui::Button(text("project.cancel", "Cancel", "CancelRename").c_str()) ||
				    ImGui::IsKeyPressed(ImGuiKey_Escape))
					ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		return changed;
	}
}
