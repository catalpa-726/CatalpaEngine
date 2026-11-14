#include "Panels/HierarchyPanel.h"

#include "Commands/SceneCommands.h"
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
	    SceneDocument& doc, EditorSelection& selection, const PanelText& text, std::string& error) {
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
		ImGui::SameLine();
		ImGui::BeginDisabled(!chosen.HasValue());
		if (ImGui::Button(text("hierarchy.delete", "Delete subtree", "DeleteEntity").c_str()) &&
		    DeleteAuthorEntity(doc, chosen.persistent, error)) {
			selection.Clear();
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SeparatorText(text("hierarchy.root", "Scene root", nullptr).c_str());
		acceptParent({});

		/* 显式遍历栈，深层作者层级不会耗尽 UI 调用栈。 */
		struct Pending {
			const Row* row;
			bool close;
		};

		std::vector<Pending> stack;
		auto push = [&](const std::vector<Row>& rows) {
			for (auto i = rows.rbegin(); i != rows.rend(); ++i)
				stack.push_back({&*i, false});
		};
		const bool searching = m_Search.front() != 0;
		if (searching) {
			for (const auto& [parent, rows] : children)
				for (const auto& row : rows)
					if (ImStristr(row.name.c_str(), nullptr, m_Search.data(), nullptr))
						stack.push_back({&row, false});
			std::reverse(stack.begin(), stack.end());
		} else {
			push(children[EntityId{}.index]);
		}
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
			const bool open = ImGui::TreeNodeEx(row.id.ToString().c_str(), flags, "     %s", row.name.c_str());
			DrawIcon(row.icon,
			    ImVec2(cursor.x + ImGui::GetTreeNodeToLabelSpacing(),
			        cursor.y + (ImGui::GetTextLineHeight() - iconSize) * .5f),
			    iconSize);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
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
			if (open && !leaf) {
				stack.push_back({nullptr, true});
				push(child->second);
			}
		}
		if (reparent && ReparentEntity(doc, reparent->first, reparent->second, error))
			changed = true;
		return changed;
	}
}
