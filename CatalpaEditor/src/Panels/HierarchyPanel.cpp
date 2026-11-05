#include "Panels/HierarchyPanel.h"
#include "Commands/SceneCommands.h"
#include "imgui.h"

namespace Catalpa::Editor {
    bool HierarchyPanel::Draw(SceneDocument& doc, EditorSelection& selection, const PanelText& text,
                              std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene)
            return false;

        struct Row {
            EntityId entity;
            PersistentEntityId id;
            std::string name;
        };

        std::map<uint32_t, std::vector<Row>> children;
        auto collected = scene->GetWorld().ForEach([&](EntityId entity) {
            IdentityComponent id;
            RelationshipComponent relation;
            if (auto s = scene->GetIdentity(entity, id); !s.IsSuccess())
                return s;
            if (auto s = scene->GetRelationship(entity, relation); !s.IsSuccess())
                return s;
            children[relation.parent.index].push_back({entity, id.persistent, std::move(id.name)});
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
            if (const auto* p = ImGui::AcceptDragDropPayload("CAT_ENTITY");
                p && p->DataSize == sizeof(Drag)) {
                Drag value;
                std::memcpy(&value, p->Data, sizeof(value));
                if (value.document == doc.Id() && scene->Resolve(value.entity).HasValue())
                    reparent.emplace(value.entity.persistent, parent);
            }
            ImGui::EndDragDropTarget();
        };
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

        // 显式遍历栈，深层作者层级不会耗尽 UI 调用栈。
        struct Pending {
            const Row* row;
            bool close;
        };

        std::vector<Pending> stack;
        auto push = [&](const std::vector<Row>& rows) {
            for (auto i = rows.rbegin(); i != rows.rend(); ++i)
                stack.push_back({&*i, false});
        };
        push(children[EntityId{}.index]);
        while (!stack.empty()) {
            const auto entry = stack.back();
            stack.pop_back();
            if (entry.close) {
                ImGui::TreePop();
                continue;
            }
            const auto& row = *entry.row;
            const auto child = children.find(row.entity.index);
            const bool leaf = child == children.end();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (leaf)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (row.entity == current)
                flags |= ImGuiTreeNodeFlags_Selected;
            const bool open = ImGui::TreeNodeEx(row.id.ToString().c_str(), flags, "%s", row.name.c_str());
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
