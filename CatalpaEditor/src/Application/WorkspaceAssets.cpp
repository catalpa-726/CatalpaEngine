#include "Application/WorkspaceEditing.h"
#include "Assets/BuiltinAssetIds.h"
#include "AssetPipeline/AssetBuildContext.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Commands/PrefabCommands.h"
#include "Commands/SceneCommands.h"
#include "Core/Application.h"
#include "Documents/DocumentRecovery.h"
#include "Panels/BuildPanel.h"
#include "Panels/EditorText.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Editor {
	namespace fs = std::filesystem;
	namespace {
		/** @brief 将脚本/图的作者文档映射到导入器使用的主产物类型。 */
		std::string_view SourceType(const fs::path& path) {
			const auto ext = Tools::AssetSourceExtension(path);
			if (ext == ".lua" || ext == ".script")
				return "script";
			if (ext == ".behavior")
				return "behavior_graph";
			if (ext == ".matgraph" || ext == ".material")
				return "material_instance";
			return {};
		}

		/** @brief 识别可绑定的材质实例；同后缀的着色定义和无效源文件不进入选择列表。 */
		bool IsMaterialInstanceFile(const fs::path& path) {
			std::string source, error;
			if (!DocumentRecovery::Read(path, source, error))
				return false;
			const auto value = nlohmann::json::parse(source, nullptr, false);
			const auto kind = value.find("kind");
			return kind != value.end() && *kind == "instance";
		}

		/** @brief 按项目的导入根和资产键读取可绑定的脚本、行为图和材质源索引。 */
		bool CollectSources(
		    const Tools::ProjectConfig& project, WorkspaceAssetActions& actions, std::string& error) try {
			std::vector<WorkspaceAssetActions::Source> rows;
			const auto collect = [&](const std::string& mount, const fs::path& root) {
				for (const auto& file :
				    fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
					if (file.is_symlink() || !file.is_regular_file())
						continue;
					const auto type = SourceType(file.path());
					if (type.empty() ||
					    (Tools::AssetSourceExtension(file.path()) == ".material" &&
					        !IsMaterialInstanceFile(file.path())))
						continue;
					Tools::AssetBuildContext context({project, file.path(), actions.shaders});
					if (!context.Initialize(error))
						return false;
					rows.push_back(
					    {file.path(), mount + "/" + Tools::AssetPathText(file.path().lexically_relative(root)),
					        context.Identity(type), type});
				}
				return true;
			};
			if (!collect("Content", project.contentRoot))
				return false;
			for (const auto& [name, root] : project.externalSourceRoots)
				if (!collect(name, root))
					return false;
			std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.label < b.label; });
			actions.sources = std::move(rows);
			actions.indexed = true;
			return true;
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}

	void WorkspaceEditing::OpenSourceAsset(const fs::path& path) {
		m_Error.clear();
		if (!m_Project || m_PlayDocument.HasValue() || !OpenAuthor(path))
			return;
		auto* doc = dynamic_cast<AssetDocument*>(m_Documents.Active());
		const auto type = doc && Tools::AssetSourceExtension(path) == ".material" ? doc->TypeKey() : SourceType(path);
		if (!doc || type.empty() || m_AssetActions.shaders.empty())
			return;
		auto recipe = std::make_shared<Tools::AssetBuildRequest>();
		recipe->project = *m_Project;
		recipe->source = path;
		recipe->shaderRoot = m_AssetActions.shaders;
		Tools::AssetBuildContext context(*recipe);
		if (!context.Initialize(m_Error))
			return;
		doc->SetSourceIdentity(context.Identity(type));
		doc->AttachRecipe(recipe);
	}

	bool WorkspaceEditing::StartSourceAction(const fs::path& source, AuthorAssetAction action, SceneDocument* doc,
	    PersistentEntityId entity, Math::Vec3 position) {
		auto& state = m_AssetActions;
		if (!m_Project || !state.build || !doc || doc->Playing() || !doc->Resolve() || m_PlayDocument.HasValue() ||
		    state.build->Busy() || state.action != AuthorAssetAction::None) {
			m_Error = "A source action requires an idle build and the active author scene";
			return false;
		}
		Tools::AssetBuildRequest request{*m_Project, source, state.shaders};
		if (action == AuthorAssetAction::PlaceModel) {
			const auto* settings = state.build->ImportSettings(request, m_Error);
			if (!settings)
				return false;
			request.settings = *settings;
		}
		auto* open = m_Documents.FindByPath(source, m_Error);
		if (open && open->History().Dirty() && !SaveDocument(*open))
			return false;
		if (!state.build->Start(std::move(request), m_Error,
		        action == AuthorAssetAction::PlaceModel ? m_Project->contentRoot : fs::path{}))
			return false;
		state.action = action;
		state.generation = state.build->Generation();
		state.source = source;
		state.document = doc->Id();
		state.position = position;
		state.entity =
		    entity.HasValue() ? doc->Resolve()->MakeReference(doc->Resolve()->Resolve(entity)) : EntityReference{};
		return true;
	}

	void WorkspaceEditing::DropSourceIntoScene(const fs::path& path, Math::Vec3 position) {
		const auto ext = Tools::AssetSourceExtension(path);
		if (ext == ".prefab")
			StartSourceAction(path, AuthorAssetAction::PlacePrefab, ActiveScene(), {}, position);
		else if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj")
			StartSourceAction(path, AuthorAssetAction::PlaceModel, ActiveScene(), {}, position);
		else {
			m_Error = Tools::AssetPathText(path.filename()) + "\n";
			const auto type = SourceType(path);
			if (type.empty()) {
				m_Error += UiText("assetwork.viewport_drop_types",
				    "Drag a model (.obj, .fbx, .gltf, .glb) or a prefab into the scene. For OBJ, drag the .obj file. The importer reads its .mtl and textures.");
				return;
			}
			m_Error += UiText("panel.inspector", "Inspector");
			m_Error += " → ";
			if (type == "script")
				m_Error += UiText("assetwork.choose_script", "Choose Lua script...");
			else if (type == "behavior_graph")
				m_Error += UiText("assetwork.choose_graph", "Choose behavior graph...");
			else
				m_Error += UiText("assetwork.material_slot", "Material slot");
		}
	}

	void WorkspaceEditing::UpdateAssetActions(BuildPanel* build, const fs::path& shaderRoot) {
		auto& state = m_AssetActions;
		if (!m_Project || state.project != m_Project->id) {
			state = {};
			if (m_Project)
				state.project = m_Project->id;
		}
		state.build = build;
		state.shaders = shaderRoot;
		if (!m_Project || !build)
			return;
		auto* doc = ActiveScene();
		if (build->TakePlacementRequest()) {
			PersistentEntityId root;
			if (doc && build->PlaceModel(*doc, {}, root, m_Error)) {
				m_Selection.Select(*doc, doc->Resolve()->Resolve(root));
				m_Documents.Activate(doc->Id());
			}
		}
		if (state.action == AuthorAssetAction::None || build->Busy())
			return;
		const auto action = std::exchange(state.action, AuthorAssetAction::None);
		if (build->Generation() != state.generation || !build->Ready() || !build->Recipe()) {
			m_Error = Tools::AssetPathText(state.source) + ": " +
			    (build->Diagnostic().empty() ? "Build cancelled or superseded" : build->Diagnostic());
			return;
		}
		if (!doc || doc->Id() != state.document || doc->Playing() || !doc->Resolve()) {
			m_Error = "The original author scene is no longer active";
			return;
		}
		const auto asset = build->Result().primary;
		if (auto* saved = dynamic_cast<AssetDocument*>(m_Documents.FindByPath(build->Recipe()->source, m_Error))) {
			saved->SetSourceIdentity(asset);
			saved->AttachRecipe(build->Recipe());
		}
		PersistentEntityId root;
		if (action == AuthorAssetAction::PlaceModel) {
			if (!build->PlaceModel(*doc, state.position, root, m_Error, asset))
				return;
		} else if (action == AuthorAssetAction::PlacePrefab) {
			if (!InstantiateAuthorPrefab(*doc, m_App.GetAssets(), asset, {}, root, m_Error, state.position))
				return;
		} else {
			const auto target = doc->Resolve()->Resolve(state.entity);
			if (!target.HasValue()) {
				m_Error = "The selected entity was removed or replaced before the build completed";
				return;
			}
			root = state.entity.persistent;
			if (action == AuthorAssetAction::BindMaterial) {
				MeshRendererComponent renderer;
				if (!doc->Resolve()->GetWorld().Get(target, renderer).IsSuccess() ||
				    renderer.meshVersion.GetId() != state.materialMesh.GetId() ||
				    renderer.meshVersion.Get() != state.materialMesh.Get() ||
				    state.materialSlot >= renderer.materials.size() ||
				    renderer.materials[state.materialSlot].GetId() != state.previousMaterial.GetId() ||
				    renderer.materials[state.materialSlot].GetVersion() != state.previousMaterial.GetVersion()) {
					m_Error = "The material slot changed before the build completed";
					return;
				}
				AssetVersionRef<MaterialInstance> material;
				bool fallback = false;
				if (!m_App.GetAssets().Acquire(asset, material, fallback, m_Error) || fallback) {
					m_Error = "The dropped source did not build a material instance: " + m_Error;
					return;
				}
				if (renderer.materials[state.materialSlot].Get() != material.Get() ||
				    renderer.materials[state.materialSlot].GetId() != material.GetId() ||
				    renderer.materials[state.materialSlot].GetVersion() != material.GetVersion()) {
					renderer.materials[state.materialSlot] = std::move(material);
					if (!SetAuthorMesh(*doc, root, renderer, m_Error))
						return;
				}
			} else if (action == AuthorAssetAction::BindScript) {
				ScriptComponent script;
				auto& world = doc->Resolve()->GetWorld();
				if (world.Has<ScriptComponent>(target))
					(void)world.Get(target, script);
				script.script = asset;
				if (!EditScript(*doc, root, script, m_Error))
					return;
			} else {
				BehaviorGraphComponent graph;
				auto& world = doc->Resolve()->GetWorld();
				if (world.Has<BehaviorGraphComponent>(target))
					(void)world.Get(target, graph);
				if (graph.graph != asset)
					graph.variables.clear();
				graph.graph = asset;
				if (!EditBehaviorGraph(*doc, root, graph, m_Error))
					return;
			}
		}
		m_Selection.Select(*doc, doc->Resolve()->Resolve(root));
		m_Documents.Activate(doc->Id());
	}

	void WorkspaceEditing::OpenBoundMaterial(const AssetVersionRef<MaterialInstance>& material) {
		if (m_Project) {
			if (!m_AssetActions.indexed && !CollectSources(*m_Project, m_AssetActions, m_Error))
				return;
			const auto source = std::find_if(m_AssetActions.sources.begin(), m_AssetActions.sources.end(),
			    [&](const auto& row) { return row.type == "material_instance" && row.id == material.GetId(); });
			if (source != m_AssetActions.sources.end()) {
				OpenSourceAsset(source->path);
				return;
			}
		}
		if (auto* draft = m_Documents.Open(material, m_Error)) {
			m_OpenAsset = draft->Id();
			ActivateAsset(*draft);
		}
	}

	void WorkspaceEditing::DrawMaterialPicker(
	    SceneDocument& doc, PersistentEntityId id, size_t slot, const MeshRendererComponent& mesh) {
		auto& state = m_AssetActions;
		const auto& bound = mesh.materials[slot];
		const auto boundId = bound.GetId();
		const auto source = std::find_if(state.sources.begin(), state.sources.end(),
		    [&](const auto& row) { return row.type == "material_instance" && row.id == boundId; });
		std::string caption = UiText("assetwork.choose_material", "Choose material");
		if (source != state.sources.end())
			caption = source->label;
		else if (boundId == BuiltinAssetIds::DefaultUnlitInstance)
			caption = UiText("assetwork.default_unlit", "Default unlit material");
		else if (boundId == BuiltinAssetIds::DefaultPbrInstance)
			caption = UiText("assetwork.default_pbr", "Default PBR material");
		else if (bound.HasValue() && bound.Get()->definition.HasValue())
			caption = bound.Get()->definition.Get()->family;

		const auto bind = [&](const fs::path& file) {
			if (StartSourceAction(file, AuthorAssetAction::BindMaterial, &doc, id)) {
				state.materialSlot = slot;
				state.materialMesh = mesh.meshVersion;
				state.previousMaterial = bound;
			}
		};
		const auto assign = [&](AssetId asset) {
			AssetVersionRef<MaterialInstance> version;
			bool fallback = false;
			if (!m_App.GetAssets().Acquire(asset, version, fallback, m_Error) || fallback)
				return;
			if (version.Get() != bound.Get() || version.GetId() != boundId ||
			    version.GetVersion() != bound.GetVersion()) {
				auto next = mesh;
				next.materials[slot] = std::move(version);
				SetAuthorMesh(doc, id, next, m_Error);
			}
		};
		ImGui::BeginDisabled(
		    m_Project && (!state.build || state.build->Busy() || state.action != AuthorAssetAction::None));
		if (ImGui::BeginCombo("##MaterialChoice", caption.c_str())) {
			if (!m_Project) {
				/* 内置示例材质只用于未打开项目时的会话预览，项目槽位引用可烘焙资源。 */
				ImGui::TextWrapped("%s",
				    UiText("assetwork.session_materials",
				        "Session preview. Open a project to use and save project materials."));
				if (ImGui::Selectable(UiText("assetwork.default_unlit", "Default unlit material"),
				        boundId == BuiltinAssetIds::DefaultUnlitInstance))
					assign(BuiltinAssetIds::DefaultUnlitInstance);
				if (ImGui::Selectable(UiText("assetwork.default_pbr", "Default PBR material"),
				        boundId == BuiltinAssetIds::DefaultPbrInstance))
					assign(BuiltinAssetIds::DefaultPbrInstance);
				ImGui::EndCombo();
				ImGui::EndDisabled();
				return;
			}
			if (ImGui::IsWindowAppearing()) {
				state.filter = {};
				CollectSources(*m_Project, state, m_Error);
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##FilterMaterial", UiText("assetwork.filter", "Filter by name or path"),
			    state.filter.data(), state.filter.size());
			if (ImGui::Selectable(UiText("assetwork.new_attach", "Create, save and attach..."))) {
				auto* previous = m_Documents.Active();
				CreateAssetDraft("material_instance");
				auto* draft = m_Documents.Active();
				if (draft && draft != previous && draft->TypeKey() == "material_instance" && SaveDocument(*draft))
					bind(draft->AuthorPath());
			}
			const auto selected = state.build->SelectedAsset();
			AssetVersionRef<MaterialInstance> built;
			bool fallback = false;
			std::string ignored;
			if (selected.HasValue() && m_App.GetAssets().Acquire(selected, built, fallback, ignored) && !fallback &&
			    ImGui::Selectable(UiText("assetwork.built_material", "Use material selected in Build")))
				assign(selected);
			for (const auto& row : state.sources)
				if (row.type == "material_instance" && row.label.find(state.filter.data()) != std::string::npos &&
				    ImGui::Selectable(row.label.c_str(), row.id == boundId))
					bind(row.path);
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\n%s", caption.c_str(),
			    UiText("assetwork.choose_drop_material", "Choose a material or drop a material asset here."));
		if (m_Project && ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload("CATALPA_SOURCE")) {
				const auto* data = static_cast<const char*>(payload->Data);
				if (data && payload->DataSize > 1 && data[payload->DataSize - 1] == 0) {
					const auto file = fs::u8path(data);
					if (SourceType(file) == "material_instance")
						bind(file);
					else
						m_Error = UiText("assetwork.material_drop_error",
						    "This slot accepts material instances and material graphs.");
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::EndDisabled();
	}

	void WorkspaceEditing::DrawBehaviorPicker(SceneDocument& doc, PersistentEntityId id, bool script, AssetId bound) {
		if (!m_Project)
			return;
		auto& state = m_AssetActions;
		const auto action = script ? AuthorAssetAction::BindScript : AuthorAssetAction::BindGraph;
		const auto type = script ? "script" : "behavior_graph";
		bool requested = m_Inspector.TakeBehaviorPicker(script);
		requested |= ImGui::Button(script ? UiText("assetwork.choose_script", "Choose Lua script...")
		                                  : UiText("assetwork.choose_graph", "Choose behavior graph..."));
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload("CATALPA_SOURCE")) {
				const char* data = static_cast<const char*>(payload->Data);
				if (data && payload->DataSize > 1 && data[payload->DataSize - 1] == 0) {
					const auto path = fs::u8path(data);
					if (SourceType(path) == type)
						StartSourceAction(path, action, &doc, id);
					else
						m_Error = "The dropped source is incompatible with this component";
				}
			}
			ImGui::EndDragDropTarget();
		}
		if ((bound.HasValue() && !state.indexed) || requested)
			CollectSources(*m_Project, state, m_Error);
		if (requested) {
			state.filter = {};
			ImGui::OpenPopup("##SelectSource");
		}
		if (ImGui::BeginPopup("##SelectSource")) {
			ImGui::InputTextWithHint("##Filter", UiText("assetwork.filter", "Filter by name or path"),
			    state.filter.data(), state.filter.size());
			if (ImGui::Selectable(UiText("assetwork.new_attach", "Create, save and attach..."))) {
				CreateAssetDraft(type);
				auto* draft = m_Documents.Active();
				if (draft && draft->TypeKey() == type && SaveDocument(*draft))
					StartSourceAction(draft->AuthorPath(), action, &doc, id);
			}
			for (const auto& row : state.sources)
				if (row.type == type && row.label.find(state.filter.data()) != std::string::npos &&
				    ImGui::Selectable(row.label.c_str(), row.id == bound))
					StartSourceAction(row.path, action, &doc, id);
			ImGui::EndPopup();
		}
		if (!bound.HasValue())
			return;
		const auto it = std::find_if(state.sources.begin(), state.sources.end(),
		    [&](const auto& row) { return row.id == bound && row.type == type; });
		if (it == state.sources.end()) {
			ImGui::TextWrapped(
			    "%s: %s", UiText("assetwork.missing_source", "Source unavailable"), bound.ToString().c_str());
			return;
		}
		ImGui::TextWrapped("%s", it->label.c_str());
		if (ImGui::Button(UiText("assetwork.open_source", "Open source")))
			OpenSourceAsset(it->path);
		ImGui::SameLine();
		if (ImGui::Button(UiText("assetwork.rebuild", "Rebuild")))
			StartSourceAction(it->path, action, &doc, id);
	}
}
