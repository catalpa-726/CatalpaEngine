#include "Application/WorkspaceEditing.h"
#include "Core/Application.h"
#include "Commands/SceneCommands.h"
#include "Commands/PrefabCommands.h"
#include "Documents/PrefabDocument.h"
#include "Scene/Components/PrefabInstanceComponent.h"
#include "Scene/PrefabReconciler.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Core/Serialization/AuthorJson.h"
#include "Panels/EditorText.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		/** @brief 按场景中的实体名称选择引用，持久标识仅用于区分同名行和保存引用。 */
		bool EntityReferenceField(Scene& scene, const char* label, PersistentEntityId& value, std::string& error) {
			IdentityComponent current;
			const auto resolved = scene.Resolve(value);
			const bool found = resolved.HasValue() && scene.GetIdentity(resolved, current).IsSuccess();
			if (!ImGui::BeginCombo(label, found ? current.name.c_str() : UiText("entity.missing", "Missing entity")))
				return false;
			bool changed = false;
			const auto status = scene.GetWorld().ForEach([&](EntityId entity) {
				IdentityComponent identity;
				const auto result = scene.GetIdentity(entity, identity);
				if (result.IsSuccess()) {
					ImGui::PushID(identity.persistent.ToString().c_str());
					if (ImGui::Selectable(identity.name.c_str(), value == identity.persistent) &&
					    value != identity.persistent) {
						value = identity.persistent;
						changed = true;
					}
					ImGui::PopID();
				}
				return result;
			});
			ImGui::EndCombo();
			if (!status.IsSuccess())
				error = status.message;
			return changed;
		}
		/**
		 * @brief 为脚本属性或行为变量绘制与实际值类型匹配的控件并报告修改。
		 *
		 * @param[in] scene 解析实体引用的所属场景。
		 * @param[in,out] values 本次操作使用的脚本值。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Properties(Scene& scene, ScriptValues& values, std::string& error) {
			bool changed = false;
			std::optional<std::string> removed;
			for (auto& [name, value] : values) {
				ImGui::PushID(name.c_str());
				changed |= std::visit(
				    [&](auto& v) -> bool {
					    using T = std::decay_t<decltype(v)>;
					    if constexpr (std::is_same_v<T, bool>)
						    return ImGui::Checkbox(name.c_str(), &v);
					    else if constexpr (std::is_same_v<T, int64_t>)
						    return ImGui::InputScalar(name.c_str(), ImGuiDataType_S64, &v, nullptr, nullptr, nullptr,
						        ImGuiInputTextFlags_EnterReturnsTrue);
					    else if constexpr (std::is_same_v<T, double>)
						    return ImGui::InputDouble(
						        name.c_str(), &v, 0, 0, "%.8g", ImGuiInputTextFlags_EnterReturnsTrue);
					    else if constexpr (std::is_same_v<T, Math::Vec3>)
						    return ImGui::InputFloat3(name.c_str(), &v.x, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
					    else if constexpr (std::is_same_v<T, PersistentEntityId>)
						    return EntityReferenceField(scene, name.c_str(), v, error);
					    else {
						    std::vector<char> buffer(std::max<size_t>(v.size() + 1, 65537), 0);
						    std::copy(v.begin(), v.end(), buffer.begin());
						    if (ImGui::InputText(
						            name.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
							    v = buffer.data();
							    return true;
						    }
						    return false;
					    }
				    },
				    value);
				ImGui::SameLine();
				if (ImGui::SmallButton(UiLabel("ui.remove_property", "Remove property").c_str()))
					removed = name;
				ImGui::PopID();
			}
			if (removed) {
				values.erase(*removed);
				changed = true;
			}
			return changed;
		}
	}

	/**
	 * @brief 为场景实体绘制可编辑的作者组件控件。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 */
	void WorkspaceEditing::AuthorComponentControls(SceneDocument& doc) {
		auto* scene = doc.Resolve();
		const auto entity = m_Selection.Resolve(doc);
		if (!scene || !entity.HasValue() || doc.Playing())
			return;
		const auto id = scene->MakeReference(entity).persistent;
		ImGui::Separator();
		ImGui::PushID("ScriptComponent");
		ScriptComponent script;
		const bool scripted =
		    scene->GetWorld().Has<ScriptComponent>(entity) && scene->GetWorld().Get(entity, script).IsSuccess();
		if (scripted) {
			m_Inspector.RevealedSection().ScrollTo("component.script");
			bool changed = ImGui::Checkbox(UiLabel("ui.script_enabled", "Script enabled").c_str(), &script.enabled);
			changed |= Properties(*scene, script.properties, m_Error);
			ImGui::InputText(UiLabel("ui.new_script_property", "New script property").c_str(), m_PropertyName.data(),
			    m_PropertyName.size());
			ImGui::Combo(UiLabel("ui.property_type", "Property type").c_str(), &m_PropertyType,
			    UiChoices({UiText("choice.bool", "Bool"), UiText("choice.integer", "Integer"),
			                  UiText("choice.number", "Number"), UiText("choice.string", "String"),
			                  UiText("choice.float3", "Float3"), UiText("choice.entity", "Entity")})
			        .c_str());
			if (ImGui::Button(UiLabel("ui.add_property", "Add property").c_str()) && m_PropertyName[0]) {
				ScriptValue value;
				switch (m_PropertyType) {
				case 0:
					value = false;
					break;
				case 1:
					value = int64_t(0);
					break;
				case 2:
					value = 0.0;
					break;
				case 3:
					value = std::string{};
					break;
				case 4:
					value = Math::Vec3{0};
					break;
				default:
					value = id;
					break;
				}
				if (!script.properties.emplace(m_PropertyName.data(), std::move(value)).second)
					m_Error = "Script property already exists";
				else
					changed = true;
			}
			if (changed)
				EditScript(doc, id, script, m_Error);
			if (ImGui::Button(UiLabel("ui.remove_script", "Remove script").c_str()))
				EditScript(doc, id, std::nullopt, m_Error);
		}
		DrawBehaviorPicker(doc, id, true, scripted ? script.script : AssetId{});
		ImGui::PopID();
		ImGui::PushID("BehaviorGraphComponent");
		BehaviorGraphComponent behavior;
		const bool graphed = scene->GetWorld().Has<BehaviorGraphComponent>(entity) &&
		    scene->GetWorld().Get(entity, behavior).IsSuccess();
		if (graphed) {
			m_Inspector.RevealedSection().ScrollTo("component.behavior");
			bool changed =
			    ImGui::Checkbox(UiLabel("ui.behavior_enabled", "Behavior enabled").c_str(), &behavior.enabled);
			changed |= Properties(*scene, behavior.variables, m_Error);
			AssetVersionRef<BehaviorGraphAsset> program;
			bool fallback = false;
			std::string ignored;
			if (m_App.GetAssets().Acquire(behavior.graph, program, fallback, ignored) && !fallback &&
			    ImGui::BeginCombo(UiLabel("ui.add_initial_variable_override", "Add initial variable override").c_str(),
			        UiText("ui.choose_a_declared_variable", "Choose a declared variable"))) {
				for (const auto& [name, value] : program.Get()->variables)
					if (!behavior.variables.contains(name) && ImGui::Selectable(name.c_str())) {
						behavior.variables.emplace(name, value);
						changed = true;
					}
				ImGui::EndCombo();
			}
			if (changed)
				EditBehaviorGraph(doc, id, behavior, m_Error);
			if (ImGui::Button(UiLabel("ui.remove_behavior", "Remove behavior").c_str()))
				EditBehaviorGraph(doc, id, std::nullopt, m_Error);
		}
		DrawBehaviorPicker(doc, id, false, graphed ? behavior.graph : AssetId{});
		ImGui::PopID();
		PrefabInstanceComponent prefab;
		if (scene->GetWorld().Has<PrefabInstanceComponent>(entity) &&
		    scene->GetWorld().Get(entity, prefab).IsSuccess()) {
			m_Inspector.RevealedSection().ScrollTo("component.prefab");
			ImGui::Separator();
			const auto& sources = m_AssetActions.sources;
			const auto source = std::find_if(
			    sources.begin(), sources.end(), [&](const auto& entry) { return entry.id == prefab.source; });
			ImGui::TextWrapped(
			    "%s", source != sources.end() ? source->label.c_str() : UiText("asset.type.prefab", "Prefab"));
			if (ImGui::Button(
			        UiLabel("ui.update_base_and_preserve_overrides", "Update base and preserve overrides").c_str()) &&
			    UpdateAuthorPrefab(doc, m_App.GetAssets(), id, false, m_Error)) {
				m_Selection.Select(doc, scene->Resolve(id));
				m_OverrideRoot = {};
			}
			if (ImGui::Button(
			        UiLabel("ui.revert_supported_overrides_to_loaded_base", "Revert supported overrides to loaded base")
			            .c_str()) &&
			    UpdateAuthorPrefab(doc, m_App.GetAssets(), id, true, m_Error)) {
				m_Selection.Select(doc, scene->Resolve(id));
				m_OverrideRoot = {};
			}
			const bool refresh =
			    ImGui::Button(UiLabel("ui.refresh_current_override_paths", "Refresh current override paths").c_str());
			if (refresh || m_OverrideRoot != id) {
				m_OverrideRoot = id;
				m_OverridePaths.clear();
				(void)PrefabReconciler::DescribeOverrides(*scene, id, m_OverridePaths, m_Error);
			}
			for (const auto& path : m_OverridePaths)
				ImGui::TextWrapped(UiFormat("ui.override", "Override %s"), path.c_str());
		}
	}

	/**
	 * @brief 绘制预制体实例及其覆盖项的操作菜单。
	 */
	void WorkspaceEditing::PrefabMenu() {
		auto* scene = ActiveScene();
		if (!scene || scene->Playing())
			return;
		if (ImGui::MenuItem(
		        UiLabel("ui.create_prefab_from_selected_subtree", "Create prefab from selected subtree").c_str())) {
			const auto entity = m_Selection.Resolve(*scene);
			PrefabAsset prefab;
			std::string text;
			if (entity.HasValue() &&
			    PrefabDocument::FromSelection(
			        *scene, scene->Resolve()->MakeReference(entity).persistent, prefab, m_Error) &&
			    WritePrefab(prefab, text, m_Error)) {
				StableId random;
				AssetId asset;
				if (GenerateStableId(random, m_Error) && AssetId::TryParse(random.ToString(), asset))
					if (auto* doc = m_Documents.OpenSource(asset, "prefab", std::move(text), {}, m_Error)) {
						doc->History().MarkUnsaved();
						m_OpenAsset = doc->Id();
						ActivateAsset(*doc);
					}
			} else if (m_Error.empty())
				m_Error = "Select an author entity first";
		}
		auto* prefab = dynamic_cast<AssetDocument*>(m_Documents.Find(m_OpenAsset));
		if (prefab && prefab->TypeKey() == "prefab" &&
		    ImGui::MenuItem(
		        UiLabel("ui.instantiate_opened_cooked_prefab", "Instantiate opened cooked prefab").c_str())) {
			PersistentEntityId root;
			if (InstantiateAuthorPrefab(*scene, m_App.GetAssets(), prefab->Asset(), {}, root, m_Error)) {
				m_Selection.Select(*scene, scene->Resolve()->Resolve(root));
				m_Documents.Activate(scene->Id());
			}
		}
	}
}
