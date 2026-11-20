#include "Panels/InspectorPanel.h"

#include "Commands/SceneCommands.h"
#include "Assets/AssetManager.h"
#include "Assets/BuiltinAssetIds.h"
#include "Core/Application.h"
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
		    {"MeshRendererComponent", "edit.mesh", "Mesh renderer"}, {"ScriptComponent", "component.script", "Script"},
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

	bool InspectorSection::ScrollTo(std::string_view target) const {
		if (key != target)
			return false;
		// 使用视口内坐标，即使属性区当前在窗口裁剪区之外也可定位。
		ImGui::SetScrollFromPosY(ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y, 0.f);
		return true;
	}

	bool InspectorSection::Header(std::string_view target, const char* label, bool defaultOpen) const {
		if (ScrollTo(target))
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		return ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
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
		if (!m_PendingType.empty() && attached(m_PendingType)) {
			const auto* option = std::find_if(std::begin(Options), std::end(Options),
			    [&](const auto& entry) { return entry.type == m_PendingType; });
			if (option != std::end(Options))
				m_Revealed.key = option->key;
			m_PendingType.clear();
		}

		/* 列表直接取自 World；作者界面使用本地化的组件名称。 */
		if (ImGui::CollapsingHeader(text("component.attached", "Attached components", "AttachedComponents").c_str(),
		        ImGuiTreeNodeFlags_DefaultOpen)) {
			for (const auto& name : types) {
				const auto type = DisplayType(name);
				const auto* option =
				    std::find_if(std::begin(Options), std::end(Options), [&](const auto& o) { return type == o.type; });
				ImGui::PushID(name.c_str());
				if (option != std::end(Options)) {
					if (ImGui::Selectable(text(option->key, option->name, nullptr).c_str()))
						m_NextReveal.key = option->key;
					const bool removable = type == "CameraComponent" || type == "LightComponent" ||
					    type == "EnvironmentComponent" || type == "MeshRendererComponent";
					if (removable && ImGui::BeginPopupContextItem("Actions")) {
						if (ImGui::MenuItem(text("component.remove", "Remove component", "Remove").c_str()))
							RemoveAuthorComponent(doc, ref.persistent, type, error);
						ImGui::EndPopup();
					}
				} else if (type == "IdentityComponent" || type == "TransformComponent" ||
				    type == "RelationshipComponent" || type == "PrefabInstanceComponent") {
					const auto title = type == "IdentityComponent" ? text("component.identity", "Name", nullptr)
					    : type == "TransformComponent"             ? text("edit.transform", "Transform", nullptr)
					    : type == "PrefabInstanceComponent" ? text("component.prefab", "Prefab instance", nullptr)
					                                        : text("component.relationship", "Hierarchy", nullptr);
					if (type == "RelationshipComponent") {
						if (ImGui::TreeNodeEx("##Hierarchy", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", title.c_str())) {
							RelationshipComponent relation;
							IdentityComponent parent;
							if (scene.GetRelationship(ref.resolved, relation).IsSuccess()) {
								const auto value = scene.GetIdentity(relation.parent, parent).IsSuccess()
								    ? parent.name : text("component.none", "None", nullptr);
								ImGui::TextWrapped("%s: %s", text("component.parent", "Parent", nullptr).c_str(), value.c_str());
							}
							ImGui::TreePop();
						}
					} else if (ImGui::Selectable(title.c_str())) {
						m_NextReveal.key = type == "IdentityComponent" ? "component.identity"
						    : type == "TransformComponent" ? "edit.transform" : "component.prefab";
					}
				} else {
					auto display = std::string(type.substr(type.rfind(':') == type.npos ? 0 : type.rfind(':') + 1));
					if (display.ends_with("Component"))
						display.resize(display.size() - 9);
					for (size_t i = 1; i < display.size(); ++i)
						if (display[i] >= 'A' && display[i] <= 'Z' && display[i - 1] >= 'a' && display[i - 1] <= 'z')
							display.insert(i++, 1, ' ');
					ImGui::TextWrapped(
					    "%s (%s)", display.c_str(), text("component.readonly", "Read-only", nullptr).c_str());
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
					m_NextReveal.key = option.key;
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
		    "%s", text("component.choose_binding", "Choose a built-in resource or a source entity:", nullptr).c_str());
		if (ImGui::Selectable(mesh ? text("component.default_mesh", "Built-in cube", "DefaultMesh").c_str()
		                           : text("component.default_sky", "Default sky", "DefaultSky").c_str())) {
			if (mesh) {
				auto& assets = Application::Get().GetAssets();
				MeshRendererComponent renderer;
				AssetVersionRef<MaterialInstance> material;
				bool fallback = false;
				if (assets.Acquire(BuiltinAssetIds::CubeMesh, renderer.meshVersion, fallback, error) && !fallback &&
				    assets.Acquire(BuiltinAssetIds::DefaultPbrInstance, material, fallback, error) && !fallback) {
					renderer.mesh = renderer.meshVersion.Get()->mesh;
					renderer.materials = {material};
					SetAuthorMesh(doc, ref.persistent, renderer, error);
				}
			} else {
				EnvironmentComponent environment;
				environment.environment.id = BuiltinAssetIds::DefaultEnvironment;
				EditEnvironment(doc, ref.persistent, environment, error);
			}
			return;
		}
		EntityId source;
		const auto status = world.ForEach([&](EntityId other) {
			if (!(mesh ? world.Has<MeshRendererComponent>(other) : world.Has<EnvironmentComponent>(other)))
				return WorldStatus{};
			IdentityComponent identity;
			const auto result = scene.GetIdentity(other, identity);
			if (!result.IsSuccess())
				return result;
			if (ImGui::Selectable((identity.name + "###" + identity.persistent.ToString()).c_str()))
				source = other;
			return WorldStatus{};
		});
		if (!status.IsSuccess())
			error = status.message;
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
		if (m_Revealed.ScrollTo("component.identity"))
			ImGui::SetKeyboardFocusHere();
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
	    MaterialSlotEdit& openMaterial, std::string& error, const MaterialSlotPicker& picker) {
		auto* scene = doc.Resolve();
		if (!scene)
			return false;
		const auto ref = scene->MakeReference(selection.Resolve(doc));
		if (!ref.HasValue())
			return false;
		IdentityComponent id;
		if (!scene->GetIdentity(ref.resolved, id).IsSuccess())
			return false;
		m_Revealed = m_ComponentTarget == ref ? m_NextReveal : InspectorSection{};
		m_NextReveal = {};
		ImGui::PushID(id.persistent.ToString().c_str());
		DrawName(doc, ref, id, text, error);
		DrawComponents(doc, ref, text, error);
		m_Properties.Draw(doc, ref, text, openMaterial, error, picker, m_Revealed);
		ImGui::PopID();
		return true;
	}
}
