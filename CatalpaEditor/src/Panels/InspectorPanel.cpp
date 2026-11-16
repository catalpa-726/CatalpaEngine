#include "Panels/InspectorPanel.h"

#include "Commands/SceneCommands.h"
#include "Panels/FormLayout.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace Catalpa::Editor {
	namespace {
		struct ComponentOption {
			const char* type;
			const char* key;
			const char* name;
		};

		constexpr ComponentOption Options[]{{"CameraComponent", "edit.camera", "Camera"},
		    {"LightComponent", "edit.light", "Light"}, {"EnvironmentComponent", "edit.environment", "Environment"},
		    {"MeshRendererComponent", "edit.mesh", "Mesh renderer"}, {"ScriptComponent", "component.lua", "Lua script"},
		    {"BehaviorGraphComponent", "component.behavior", "Behavior graph"},
		    {"RigidBodyComponent", "physics.body", "Rigid body"},
		    {"CharacterControllerComponent", "physics.character", "Character controller"},
		    {"ColliderComponent", "physics.collider", "Collider"},
		    {"NavigationAgentComponent", "navigation.agent", "Navigation agent"},
		    {"TerrainComponent", "terrain.component", "Terrain"},
		    {"AudioSourceComponent", "audio.source", "Audio source"},
		    {"AudioListenerComponent", "component.listener", "Audio listener"},
		    {"UIComponent", "ui.component", "Game UI"}};

		/**
		 * @brief 提取内建类型的短名称，其他命名空间保留完整类型名，避免自定义同名组件误匹配。
		 * @param[in] type World 提供的类型全名。
		 * @return 指向原字符串的只读名称视图。
		 */
		std::string_view DisplayType(std::string_view type) {
			for (const auto prefix :
			    {std::string_view("struct "), std::string_view("class "), std::string_view("Catalpa::")})
				if (type.starts_with(prefix))
					type.remove_prefix(prefix.size());
			return type;
		}
	}

	bool InspectorPanel::TakeBehaviorPicker(bool script) {
		return std::exchange(script ? m_ScriptPicker : m_GraphPicker, false);
	}

	void InspectorPanel::DrawComponents(
	    SceneDocument& doc, EntityReference ref, const PanelText& text, std::string& error) {
		auto& scene = *doc.Resolve();
		const auto& world = scene.GetWorld();
		if (m_ComponentTarget != ref) {
			m_ComponentTarget = ref;
			m_PendingType.clear();
			m_ScriptPicker = m_GraphPicker = false;
		}
		std::vector<std::string> types;
		if (const auto status = world.ListComponentTypes(ref.resolved, types); !status.IsSuccess()) {
			error = status.message;
			return;
		}
		const auto attached = [&](std::string_view type) {
			return std::any_of(types.begin(), types.end(), [&](const auto& name) { return DisplayType(name) == type; });
		};
		if (attached(m_PendingType))
			m_PendingType.clear();

		/* 列表直接取自 World。缺少属性适配器的组件也显示完整名称，不遗漏自定义组件。 */
		if (ImGui::CollapsingHeader(text("component.attached", "Attached components", "AttachedComponents").c_str(),
		        ImGuiTreeNodeFlags_DefaultOpen)) {
			for (const auto& name : types) {
				const auto type = DisplayType(name);
				const auto* option =
				    std::find_if(std::begin(Options), std::end(Options), [&](const auto& o) { return type == o.type; });
				ImGui::PushID(name.c_str());
				if (option != std::end(Options)) {
					ImGui::TextWrapped("%s", text(option->key, option->name, nullptr).c_str());
					const bool removable = type == "CameraComponent" || type == "LightComponent" ||
					    type == "EnvironmentComponent" || type == "MeshRendererComponent";
					if (removable && ImGui::BeginPopupContextItem("Actions")) {
						if (ImGui::MenuItem(text("component.remove", "Remove component", "Remove").c_str()))
							RemoveAuthorComponent(doc, ref.persistent, type, error);
						ImGui::EndPopup();
					}
				} else if (type == "IdentityComponent" || type == "TransformComponent" ||
				    type == "RelationshipComponent" || type == "PrefabInstanceComponent") {
					ImGui::TextWrapped("%.*s", int(type.size()), type.data());
					if (type == "RelationshipComponent") {
						RelationshipComponent relation;
						IdentityComponent parent;
						if (scene.GetRelationship(ref.resolved, relation).IsSuccess() &&
						    scene.GetIdentity(relation.parent, parent).IsSuccess())
							ImGui::TextWrapped(
							    "%s: %s", text("component.parent", "Parent", nullptr).c_str(), parent.name.c_str());
					}
				} else {
					ImGui::TextWrapped(
					    "%s (%s)", name.c_str(), text("component.readonly", "Read-only", nullptr).c_str());
				}
				ImGui::PopID();
			}
		}

		/* 基础组件不在可添加列表中。已存在的类型以及与刚体互斥的角色控制器不提供重复入口。 */
		if (ImGui::Button(text("component.add", "Add component", "AddComponent").c_str())) {
			m_ComponentFilter = {};
			ImGui::OpenPopup("##ComponentPicker");
		}
		if (ImGui::BeginPopup("##ComponentPicker")) {
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::InputTextWithHint("##Search", text("assets.filter", "Filter", nullptr).c_str(),
			    m_ComponentFilter.data(), m_ComponentFilter.size());
			for (const auto& option : Options) {
				const std::string title = text(option.key, option.name, nullptr);
				const std::string_view type = option.type;
				if (attached(type) || (type == "RigidBodyComponent" && attached("CharacterControllerComponent")) ||
				    (type == "CharacterControllerComponent" && attached("RigidBodyComponent")))
					continue;
				if (m_ComponentFilter[0] && !ImStristr(title.c_str(), nullptr, m_ComponentFilter.data(), nullptr) &&
				    !ImStristr(option.type, nullptr, m_ComponentFilter.data(), nullptr))
					continue;
				if (ImGui::Selectable((title + "###" + option.type).c_str())) {
					m_PendingType = option.type;
					if (type == "CameraComponent")
						AddAuthorCamera(doc, ref.persistent, error);
					else if (type == "LightComponent")
						EditLight(doc, ref.persistent, LightComponent{}, error);
					else if (type == "ScriptComponent")
						m_ScriptPicker = true;
					else if (type == "BehaviorGraphComponent")
						m_GraphPicker = true;
				}
			}
			ImGui::EndPopup();
		}

		/* 网格和环境必须有合法资源引用。选用场景中已有的绑定，保留对应的固定版本与材质槽。 */
		if (m_PendingType != "MeshRendererComponent" && m_PendingType != "EnvironmentComponent")
			return;
		const bool mesh = m_PendingType == "MeshRendererComponent";
		ImGui::TextWrapped(
		    "%s", text("component.choose_binding", "Choose a source entity with a resource binding:", nullptr).c_str());
		EntityId source;
		size_t count = 0;
		const auto status = world.ForEach([&](EntityId other) {
			if (!(mesh ? world.Has<MeshRendererComponent>(other) : world.Has<EnvironmentComponent>(other)))
				return WorldStatus{};
			IdentityComponent identity;
			const auto result = scene.GetIdentity(other, identity);
			if (!result.IsSuccess())
				return result;
			++count;
			if (ImGui::Selectable((identity.name + "###" + identity.persistent.ToString()).c_str()))
				source = other;
			return WorldStatus{};
		});
		if (!status.IsSuccess())
			error = status.message;
		else if (!count)
			ImGui::TextWrapped(
			    "%s", text("component.no_binding", "No compatible binding exists in this scene.", nullptr).c_str());
		else if (source.HasValue()) {
			if (mesh) {
				MeshRendererComponent value;
				if (world.Get(source, value).IsSuccess())
					SetAuthorMesh(doc, ref.persistent, value, error);
			} else {
				EnvironmentComponent value;
				if (world.Get(source, value).IsSuccess())
					EditEnvironment(doc, ref.persistent, value, error);
			}
		}
		if (ImGui::SmallButton(text("project.cancel", "Cancel", "CancelBinding").c_str()))
			m_PendingType.clear();
	}

	void InspectorPanel::DrawName(SceneDocument& doc, EntityReference ref, const IdentityComponent& identity,
	    const PanelText& text, std::string& error) {
		constexpr size_t MaxNameBytes = 65536; /* 与 ComponentFieldKind::String 的作者字节上限一致。 */
		if (identity.name.size() > MaxNameBytes) {
			ImGui::TextUnformatted(identity.name.c_str());
			return;
		}
		if (m_NameBuffer.empty())
			m_NameBuffer.resize(MaxNameBytes + 1);
		std::copy(identity.name.begin(), identity.name.end(), m_NameBuffer.begin());
		m_NameBuffer[identity.name.size()] = '\0';
		PlaceFormLabel(text("edit.entity_name", "Name", nullptr));
		const bool submit = ImGui::InputText(
		    "##EntityName", m_NameBuffer.data(), m_NameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::IsItemActivated() || m_NameTarget != ref) {
			m_NameAtActivation = identity.name;
			m_NameTarget = ref;
		}
		if (submit)
			RenameEntity(doc, ref.persistent, m_NameBuffer.data(), m_NameAtActivation, error);
		if (ImGui::IsItemDeactivated())
			m_NameTarget = {};
	}

	bool InspectorPanel::Draw(SceneDocument& doc, const EditorSelection& selection, const PanelText& text,
	    AssetVersionRef<MaterialInstance>& openMaterial, std::string& error, const MaterialSlotPicker& picker) {
		auto* scene = doc.Resolve();
		if (!scene)
			return false;
		const auto ref = scene->MakeReference(selection.Resolve(doc));
		if (!ref.HasValue())
			return false;
		IdentityComponent id;
		if (!scene->GetIdentity(ref.resolved, id).IsSuccess())
			return false;
		ImGui::PushID(id.persistent.ToString().c_str());
		DrawName(doc, ref, id, text, error);
		ImGui::TextWrapped("%s", id.persistent.ToString().c_str());
		DrawComponents(doc, ref, text, error);
		m_Properties.Draw(doc, ref, text, openMaterial, error, picker);
		ImGui::PopID();
		return true;
	}
}
