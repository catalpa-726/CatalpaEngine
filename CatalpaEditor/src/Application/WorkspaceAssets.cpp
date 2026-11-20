#include "Application/WorkspaceEditing.h"
#include "Assets/BuiltinAssetIds.h"
#include "AssetPipeline/AssetBuildContext.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "AssetEditors/MaterialEditor.h"
#include "Commands/PrefabCommands.h"
#include "Commands/SceneCommands.h"
#include "Core/Application.h"
#include "Core/PlatformServices.h"
#include "Core/IO/VirtualPath.h"
#include "Scripting/ScriptAsset.h"
#include "Documents/DocumentRecovery.h"
#include "Panels/BuildPanel.h"
#include "Platform/ExternalScriptEditor.h"
#include "Platform/AuthorFileDialogs.h"
#include "Project/ScriptIdeProject.h"
#include "Panels/EditorText.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Editor {
	namespace fs = std::filesystem;
	namespace {
		/** @brief 将脚本/图的作者文档映射到导入器使用的主产物类型。 */
		std::string_view SourceType(const fs::path& path) {
			const auto ext = Tools::AssetSourceExtension(path);
			if (ext == ".cs" || ext == ".lua" || ext == ".script")
				return "script";
			if (ext == ".behavior")
				return "behavior_graph";
			if (ext == ".matgraph" || ext == ".material")
				return "material_instance";
			return {};
		}

		std::string_view ComponentSourceType(const fs::path& file) {
			const auto ext = Tools::AssetSourceExtension(file);
			if (Tools::TextureImporter::SupportsMaterialMap(file))
				return "image/color";
			if (ext == ".collision")
				return "collision_shape";
			if (ext == ".physics-material")
				return "physics_material";
			if (ext == ".terrain")
				return "terrain";
			if (ext == ".navmesh")
				return "navmesh";
			if (ext == ".ui")
				return "ui_document";
			if (ext == ".wav" || ext == ".mp3" || ext == ".flac")
				return "audio_clip";
			return {};
		}

		/** @brief 在启动场景中选择源文件，按资产类型核对后由构建任务把它导入会话目录。 */
		fs::path SelectComponentSource(const Tools::ProjectConfig* project, std::string_view type, std::string& error) {
			const char* filter = type == "script" ? "cs" : type == "behavior_graph" ? "behavior,json"
			    : type == "collision_shape" ? "collision,json" : type == "physics_material" ? "physics-material,json"
			    : type == "terrain" ? "terrain,json" : type == "navmesh" ? "navmesh,json"
			    : type == "ui_document" ? "ui,json" : "wav,mp3,flac";
			fs::path source;
			bool cancelled = false;
			if (!SelectAuthorFile(false, project ? project->contentRoot : fs::path{}, source, cancelled, error, filter) || cancelled)
				return {};
			const bool matches = type == "script" ? Tools::AssetSourceExtension(source) == ".cs"
			    : SourceType(source) == type || ComponentSourceType(source) == type;
			if (!matches) {
				error = UiText("component.incompatible_source", "Choose a compatible source file for this component.");
				return {};
			}
			return source;
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
					auto type = SourceType(file.path());
					if (type.empty())
						type = ComponentSourceType(file.path());
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

	void WorkspaceEditing::EditExternalScript(const fs::path& path) {
		m_Error.clear();
		fs::path ideProject;
		if (const auto* project = AssetProject(); project && Tools::AssetSourceExtension(path) == ".cs") {
			fs::path directory;
			if (!GetExecutableDirectory(directory, m_Error) ||
			    !Tools::PrepareScriptIdeProject(*project, path, directory / "Resources/Managed/Catalpa.Scripting.dll",
			        ideProject, m_Error))
				return;
		}
		LaunchScriptEditor(m_ScriptEditor, path, ideProject, m_Error);
	}

	void WorkspaceEditing::EditBuiltScript(AssetId id, const BuildPanel& build) {
		if (build.Recipe() && id == build.Result().primary && SourceType(build.Recipe()->source) == "script") {
			OpenSourceAsset(build.Recipe()->source);
			return;
		}
		if (const auto* project = AssetProject()) {
			if (!CollectSources(*project, m_AssetActions, m_Error))
				return;
			for (const auto& item : m_AssetActions.sources) {
				if (item.id == id && item.type == "script") {
					OpenSourceAsset(item.path);
					return;
				}
			}
		}
		m_Error = UiText("assetwork.missing_source", "Source unavailable");
	}

	void WorkspaceEditing::OpenSourceAsset(const fs::path& path) {
		m_Error.clear();
		if (SourceType(path) == "script") {
			EditExternalScript(path);
			return;
		}
		if (m_PlayDocument.HasValue() || !OpenAuthor(path))
			return;
		/* 独立文档可以直接编辑；会话 Content 中的资源与项目资源使用相同的构建配方。 */
		const auto* project = AssetProject();
		if (!project || (!m_Project && !IsPathWithinRoot(path, project->contentRoot)) || m_PendingAuthor || m_PendingScene)
			return;
		auto* doc = dynamic_cast<AssetDocument*>(m_Documents.Active());
		const auto type = doc && Tools::AssetSourceExtension(path) == ".material" ? doc->TypeKey() : SourceType(path);
		if (!doc || type.empty() || m_AssetActions.shaders.empty())
			return;
		auto recipe = std::make_shared<Tools::AssetBuildRequest>();
		recipe->project = *project;
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
		const bool component = action == AuthorAssetAction::BindScript || action == AuthorAssetAction::BindGraph ||
		    action == AuthorAssetAction::BindComponent;
		const auto* project = AssetProject();
		if (!state.build || !doc || doc->Playing() || !doc->Resolve() || m_PlayDocument.HasValue() ||
		    state.build->Busy() || state.action != AuthorAssetAction::None) {
			m_Error = "A source action requires an idle build and the active author scene";
			return false;
		}
		if (!project && component)
			project = EnsureSessionAssets(true);
		if (!project || (!m_Project && !component))
			return false;
		Tools::AssetBuildRequest request{*project, source, state.shaders};
		if (action == AuthorAssetAction::PlaceModel) {
			const auto* settings = state.build->ImportSettings(request, m_Error);
			if (!settings)
				return false;
			request.settings = *settings;
		}
		auto* open = m_Documents.FindByPath(source, m_Error);
		if (open && open->History().Dirty() && !SaveDocument(*open, false, false))
			return false;
		if (!state.build->Start(std::move(request), m_Error,
		        action == AuthorAssetAction::PlaceModel || !m_Project ? project->contentRoot : fs::path{}))
			return false;
		state.readyComponentAsset = {};
		state.project = project->id;
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
				m_Error += UiText("assetwork.choose_script", "Choose C# script...");
			else if (type == "behavior_graph")
				m_Error += UiText("assetwork.choose_graph", "Choose behavior graph...");
			else
				m_Error += UiText("assetwork.material_slot", "Material slot");
		}
	}

	void WorkspaceEditing::PreparePlay(SceneDocument& doc) {
		const auto* project = AssetProject();
		if (!project || !doc.Resolve()) {
			BeginPreparedPlay(doc);
			return;
		}
		auto& state = m_AssetActions;
		std::set<AssetId> attached;
		const auto& world = doc.Resolve()->GetWorld();
		const auto scanned = world.ForEach([&](EntityId entity) -> WorldStatus {
			if (!world.Has<ScriptComponent>(entity))
				return {};
			ScriptComponent script;
			const auto status = world.Get(entity, script);
			if (status.IsSuccess() && script.enabled && script.script.HasValue())
				attached.insert(script.script);
			return status;
		});
		if (!scanned.IsSuccess()) {
			m_Error = scanned.message;
			return;
		}
		if (attached.empty()) {
			BeginPreparedPlay(doc);
			return;
		}
		if (!CollectSources(*project, state, m_Error))
			return;
		state.playScripts.clear();
		for (auto id : attached) {
			const auto source = std::find_if(state.sources.begin(), state.sources.end(),
			    [&](const auto& row) { return row.type == "script" && row.id == id; });
			if (source != state.sources.end()) {
				if (Tools::AssetSourceExtension(source->path) == ".cs")
					state.playScripts.push_back(source->path);
				continue;
			}
			AssetVersionRef<ScriptAsset> version;
			bool fallback = false;
			std::string ignored;
			if (m_App.GetAssets().Acquire(id, version, fallback, ignored) && !fallback &&
			    version.Get()->language == ScriptLanguage::CSharp) {
				m_Error = UiText("script.missing_source", "Cannot find the attached C# source") +
				    std::string(" · ") + version.Get()->sourceLabel;
				return;
			}
		}
		state.playAfterBuild = doc.Id();
		state.playBuildGeneration = 0;
		ContinuePlayBuild(&doc);
	}

	void WorkspaceEditing::ContinuePlayBuild(SceneDocument* doc) {
		auto& state = m_AssetActions;
		if (!AssetProject() || !state.build || !doc || doc->Id() != state.playAfterBuild || doc->Playing()) {
			state.playAfterBuild = {};
			return;
		}
		if (state.playBuildGeneration && (state.build->Generation() != state.playBuildGeneration || !state.build->Ready())) {
			m_Error = state.build->Diagnostic().empty() ? UiText("script.build_cancelled", "Script build cancelled")
			                                           : state.build->Diagnostic();
			state.playAfterBuild = {};
			return;
		}
		if (state.playScripts.empty()) {
			state.playAfterBuild = {};
			BeginPreparedPlay(*doc);
			return;
		}
		if (!state.build->Start({*AssetProject(), state.playScripts.back(), state.shaders}, m_Error)) {
			state.playAfterBuild = {};
			return;
		}
		state.playScripts.pop_back();
		state.playBuildGeneration = state.build->Generation();
	}

	void WorkspaceEditing::UpdateAssetActions(BuildPanel* build, const fs::path& shaderRoot) {
		auto& state = m_AssetActions;
		const auto* project = AssetProject();
		const auto projectId = project ? project->id : StableId{};
		if (state.project != projectId) {
			state = {};
			state.project = projectId;
		}
		state.build = build;
		state.shaders = shaderRoot;
		if (!project || !build)
			return;
		auto* doc = ActiveScene();
		if (state.readyComponentAsset.HasValue() &&
		    (!doc || !doc->Resolve() || doc->Id() != state.document || doc->Playing() ||
		        doc->Resolve()->Resolve(state.entity) != m_Selection.Resolve(*doc)))
			state.readyComponentAsset = {};
		if (state.playAfterBuild.HasValue() && !build->Busy()) {
			ContinuePlayBuild(doc);
			return;
		}
		if (build->TakePlacementRequest()) {
			PersistentEntityId root;
			if (doc && build->PlaceModel(*doc, {}, root, m_Error)) {
				m_Selection.Select(*doc, doc->Resolve()->Resolve(root));
				m_Documents.Activate(doc->Id());
			}
		}
		if (!build->Busy() && state.action == AuthorAssetAction::None && !state.pendingMaterialBuilds.empty()) {
			auto next = state.pendingMaterialBuilds.extract(state.pendingMaterialBuilds.begin());
			build->Start({*project, next.value(), shaderRoot}, m_Error);
		}
		if (state.action == AuthorAssetAction::None || build->Busy())
			return;
		const auto action = std::exchange(state.action, AuthorAssetAction::None);
		/* 外部源先复制到会话目录；后续编译失败时仍可在资源面板打开并修正。 */
		if (!m_Project) {
			m_AssetFilesChanged = true;
			state.indexed = false;
		}
		if (build->Generation() != state.generation || !build->Ready() || !build->Recipe()) {
			m_Error = Tools::AssetPathText(state.source) + ": " +
			    (build->Diagnostic().empty() ? "Build cancelled or superseded" : build->Diagnostic());
			return;
		}
		if (action == AuthorAssetAction::BindTexture) {
			auto* draft = dynamic_cast<AssetDocument*>(m_Documents.Find(state.document));
			if (!draft || !draft->Material())
				return;
			const auto& current = *draft->Material();
			const auto* value = FindMaterialParameter(current, state.textureParameter);
			const auto* old = value ? std::get_if<AssetHandle<ImageAsset>>(value) : nullptr;
			if (!old || *old != state.previousTexture) {
				m_Error = UiText("material.texture_changed", "The texture slot changed during import.");
				return;
			}
			const auto& parameters = current.definition.Get()->parameters;
			const auto parameter = std::find_if(parameters.begin(), parameters.end(),
			    [&](const auto& entry) { return entry.id == state.textureParameter; });
			const auto& mappings = build->Result().mappings;
			const auto product = std::find_if(mappings.begin(), mappings.end(),
			    [&](const auto& entry) { return entry.sourceKey == state.textureKey && entry.kind == "image"; });
			if (parameter == parameters.end() || product == mappings.end()) {
				m_Error = UiText("material.texture_missing", "Imported image does not provide this texture channel.");
				return;
			}
			SetMaterialTexture(*draft, m_App.GetAssets(), *parameter, AssetHandle<ImageAsset>{product->asset}, m_Error);
			return;
		}
		if (!doc || doc->Id() != state.document || doc->Playing() || !doc->Resolve()) {
			m_Error = "The original author scene is no longer active";
			return;
		}
		const auto asset = build->Result().primary;
		if (!m_Project && !CollectSources(*project, state, m_Error))
			return;
		if (auto* saved = dynamic_cast<AssetDocument*>(m_Documents.FindByPath(build->Recipe()->source, m_Error))) {
			saved->SetSourceIdentity(asset);
			saved->AttachRecipe(build->Recipe());
		}
		PersistentEntityId root;
		if (action == AuthorAssetAction::BindComponent) {
			const auto target = doc->Resolve()->Resolve(state.entity);
			if (target.HasValue() && target == m_Selection.Resolve(*doc))
				state.readyComponentAsset = asset;
			return;
		}
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

	void WorkspaceEditing::OpenBoundMaterial(
	    SceneDocument& scene, EntityReference entity, const MaterialSlotEdit& edit) {
		MaterialTarget origin{scene.Id(), entity, edit};
		for (const auto& [id, targets] : m_MaterialTargets) {
			for (const auto& target : targets) {
				if (target.document == scene.Id() && target.entity.resolved == entity.resolved &&
				    target.original.slot == edit.slot && target.original.material.Get() == edit.material.Get()) {
					if (auto* doc = dynamic_cast<AssetDocument*>(m_Documents.Find(id))) {
						m_OpenAsset = id;
						m_AskDiscard = false;
						ActivateAsset(*doc);
						return;
					}
				}
			}
		}
		if (m_Project) {
			if (!m_AssetActions.indexed && !CollectSources(*m_Project, m_AssetActions, m_Error))
				return;
			const auto source =
			    std::find_if(m_AssetActions.sources.begin(), m_AssetActions.sources.end(), [&](const auto& row) {
				    return row.id == edit.material.GetId() && Tools::AssetSourceExtension(row.path) == ".material";
			    });
			if (source != m_AssetActions.sources.end() && IsPathWithinRoot(source->path, m_Project->contentRoot)) {
				OpenSourceAsset(source->path);
				if (m_PendingAuthor && m_PendingAuthor->path == source->path) {
					m_PendingAuthor->materialTarget = origin;
					return;
				}
				auto* draft = dynamic_cast<AssetDocument*>(m_Documents.Active());
				if (draft && draft->Material() && draft->AuthorPath() == source->path)
					m_MaterialTargets[draft->Id()].push_back(origin);
				return;
			}
		}
		/* 内置与导入子材质的编辑属于发起操作的槽。为每个槽分配独立草稿身份。 */
		StableId identity;
		AssetVersionRef<MaterialInstance> copy;
		if (!GenerateStableId(identity, m_Error) ||
		    !AssetVersionRef<MaterialInstance>::TryCreate(
		        AssetId{identity}, edit.material.GetVersion(), edit.material.GetShared(), copy, m_Error))
			return;
		if (auto* draft = m_Documents.Open(copy, m_Error)) {
			m_MaterialTargets[draft->Id()] = {origin};
			m_OpenAsset = draft->Id();
			m_AskDiscard = false;
			ActivateAsset(*draft);
		}
	}

	bool WorkspaceEditing::ValidateMaterialTarget(
	    const MaterialTarget& target, SceneDocument*& doc, MeshRendererComponent& mesh) {
		doc = dynamic_cast<SceneDocument*>(m_Documents.Find(target.document));
		const auto* scene = doc ? doc->Resolve() : nullptr;
		const auto entity = scene ? scene->Resolve(target.entity) : EntityId{};
		const auto slot = target.original.slot;
		if (!entity.HasValue() || doc->Playing() || !scene->GetWorld().Get(entity, mesh).IsSuccess() ||
		    mesh.meshVersion.Get() != target.original.mesh.Get() || slot >= mesh.materials.size() ||
		    mesh.materials[slot].Get() != target.original.material.Get() ||
		    mesh.materials[slot].GetId() != target.original.material.GetId()) {
			m_Error = UiText(
			    "material.target_changed", "The original material slot has changed. Reopen it from the Inspector.");
			return false;
		}
		return true;
	}

	bool WorkspaceEditing::ApplySavedMaterial(AssetDocument& draft, bool external, bool cookMaterial) {
		const auto binding = m_MaterialTargets.find(draft.Id());
		std::set<std::tuple<StableId, PersistentEntityId, size_t>> boundSlots;
		std::map<SceneDocument*, std::vector<AuthorMaterialSlot>> slots;
		if (binding != m_MaterialTargets.end()) {
			for (const auto& target : binding->second) {
				SceneDocument* document{};
				MeshRendererComponent mesh;
				if (!ValidateMaterialTarget(target, document, mesh))
					return false;
				if (!boundSlots.emplace(document->Id(), target.entity.persistent, target.original.slot).second)
					continue;
				slots[document].push_back({target.entity.persistent, target.original.slot,
				    mesh.meshVersion, mesh.materials[target.original.slot]});
			}
		}
		AssetId identity;
		if (external) {
			Tools::AssetBuildRequest request{*m_Project, draft.AuthorPath(), m_AssetActions.shaders};
			Tools::AssetBuildContext context(request);
			if (!context.Initialize(m_Error))
				return false;
			identity = context.Identity("material_instance");
			/* 场景持有确切的资产版本，发布相同源身份的新版本时需要更新所有仍引用它的槽。 */
			for (auto* open : m_Documents.All()) {
				auto* document = dynamic_cast<SceneDocument*>(open);
				const auto* scene = document ? document->Resolve() : nullptr;
				if (!scene || document->Playing())
					continue;
				const auto& world = scene->GetWorld();
				auto status = world.ForEach([&](EntityId entity) -> WorldStatus {
					if (!world.Has<MeshRendererComponent>(entity))
						return {};
					MeshRendererComponent mesh;
					if (auto result = world.Get(entity, mesh); !result.IsSuccess())
						return result;
					for (size_t index = 0; index < mesh.materials.size(); ++index) {
						const auto& current = mesh.materials[index];
						if (!current.HasValue() || current.GetId() != identity || current.Get()->sceneLocal)
							continue;
						const auto id = scene->MakeReference(entity).persistent;
						if (!boundSlots.contains({document->Id(), id, index}))
							slots[document].push_back({id, index, mesh.meshVersion, current});
					}
					return {};
				});
				if (!status.IsSuccess()) {
					m_Error = status.message;
					return false;
				}
			}
			if (slots.empty())
				return true;
			if (cookMaterial) {
				if (!m_AssetActions.build) {
					m_Error = "No material build service is available";
					return false;
				}
				if (m_AssetActions.build->Busy() || m_AssetActions.action != AuthorAssetAction::None)
					m_AssetActions.pendingMaterialBuilds.insert(draft.AuthorPath());
				else if (!m_AssetActions.build->Start(std::move(request), m_Error))
					return false;
			}
		} else {
			StableId generated;
			if (!GenerateStableId(generated, m_Error))
				return false;
			identity = AssetId{generated};
		}
		auto edited = std::make_shared<MaterialInstance>(*draft.Material());
		edited->sceneLocal = !external;
		AssetVersionRef<MaterialInstance> version;
		if (!AssetVersionRef<MaterialInstance>::TryCreate(identity, edited->revision, edited, version, m_Error))
			return false;
		for (auto& [scene, changes] : slots)
			if (!SetAuthorMaterialSlots(*scene, std::move(changes), version, m_Error))
				return false;
		if (binding != m_MaterialTargets.end())
			for (auto& target : binding->second)
				target.original.material = version;
		draft.History().MarkSaved();
		return true;
	}

	/** @brief 以项目源文件名称选择组件依赖，异步构建只交付给发起选择的实体和字段。 */
	bool WorkspaceEditing::DrawComponentAssetPicker(
	    SceneDocument& doc, PersistentEntityId entity, const char* label, std::string_view type, AssetId& asset) {
		auto& state = m_AssetActions;
		if (state.readyComponentAsset.HasValue() && state.document == doc.Id() && state.entity.persistent == entity &&
		    state.componentType == type) {
			const auto ready = std::exchange(state.readyComponentAsset, {});
			if (state.previousComponentAsset == asset && asset != ready) {
				asset = ready;
				return true;
			}
		}
		const auto row = std::find_if(state.sources.begin(), state.sources.end(),
		    [&](const auto& source) { return source.type == type && source.id == asset; });
		const char* caption = row != state.sources.end()  ? row->label.c_str()
		    : asset == BuiltinAssetIds::BoxCollisionShape ? UiText("component.box_shape", "Box (1 m)")
		    : asset == BuiltinAssetIds::GroundCollisionShape ? UiText("component.ground_shape", "Static ground (12 m)")
		    : asset.HasValue()                            ? UiText("component.assigned_asset", "Assigned asset")
		                                                  : UiText("component.select_asset", "Select asset");
		const bool busy = state.action != AuthorAssetAction::None || (state.build && state.build->Busy());
		ImGui::BeginDisabled(busy);
		bool changed = false;
		if (ImGui::BeginCombo(label, caption)) {
			const auto* project = AssetProject();
			if (project && ImGui::IsWindowAppearing())
				CollectSources(*project, state, m_Error);
			if (type == "collision_shape") {
				const auto shape = [&](AssetId id, const char* name) {
					if (ImGui::Selectable(name, asset == id) && asset != id) {
						asset = id;
						changed = true;
					}
				};
				shape(BuiltinAssetIds::BoxCollisionShape, UiText("component.box_shape", "Box (1 m)"));
				shape(BuiltinAssetIds::GroundCollisionShape, UiText("component.ground_shape", "Static ground (12 m)"));
			}
			if (type == "physics_material" && ImGui::Selectable(UiText("component.none", "None"), !asset.HasValue())) {
				changed = asset.HasValue();
				asset = {};
			}
			const auto bind = [&](const fs::path& source) {
				if (!source.empty() && StartSourceAction(source, AuthorAssetAction::BindComponent, &doc, entity)) {
					state.componentType = type;
					state.previousComponentAsset = asset;
				}
			};
			if (!m_Project && ImGui::Selectable(UiText("component.import_session", "Import into session...")))
				bind(SelectComponentSource(project, type, m_Error));
			bool available = false;
			for (const auto& source : state.sources) {
				if (source.type != type)
					continue;
				available = true;
				if (ImGui::Selectable(source.label.c_str(), source.id == asset) && source.id != asset)
					bind(source.path);
			}
			if (!available && m_Project)
				ImGui::TextWrapped("%s", UiText("component.no_sources", "Import a compatible asset into this project."));
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		return changed;
	}

	bool WorkspaceEditing::DrawTexturePicker(
	    AssetDocument& doc, const MaterialParameter& parameter, AssetHandle<ImageAsset>& current, const fs::path& selectedFile) {
		auto& state = m_AssetActions;
		const auto semantics = parameter.textureSemantic;
		const auto channel = semantics == MaterialTextureSemantic::Normal ? "normal"
		    : semantics == MaterialTextureSemantic::Emissive ? "emissive"
		    : semantics == MaterialTextureSemantic::BaseColor ? "baseColor" : "data";
		const auto [semantic, key] = Tools::TextureImporter::MaterialChannel(channel);
		AssetVersionRef<ImageAsset> fixed;
		bool fallback = false;
		std::string error;
		std::string caption = current.HasValue() ? UiText("material.texture_assigned", "Assigned")
		                                         : UiText("material.drop_image", "Drop a texture here");
		if (current.HasValue() && m_App.GetAssets().Acquire(current.id, fixed, fallback, error) && !fallback)
			caption = Tools::AssetPathText(fs::u8path(fixed.Get()->sourceUri).filename());
		const bool pending = state.action == AuthorAssetAction::BindTexture && state.document == doc.Id() &&
		    state.textureParameter == parameter.id;
		if (pending)
			caption = UiText("material.texture_loading", "Importing image...");
		const bool busy = m_Project && (!state.build || state.build->Busy() || state.action != AuthorAssetAction::None);
		bool clear = false;
		fs::path source;
		ImGui::BeginDisabled(busy);
		if (ImGui::BeginCombo("##TextureSource", caption.c_str())) {
			std::error_code ec;
			if (Tools::TextureImporter::SupportsMaterialMap(selectedFile) && fs::is_regular_file(selectedFile, ec)) {
				const std::string useSelected = std::string(UiText("material.use_selected_image", "Use selected texture")) +
				    " · " + Tools::AssetPathText(selectedFile.filename()) + "###UseSelectedTexture";
				if (ImGui::Selectable(useSelected.c_str()))
					source = selectedFile;
			}
			const auto browse = std::string(UiText("material.browse_assets", "Show Assets panel")) + "###BrowseTextureAssets";
			if (ImGui::Selectable(browse.c_str()))
				m_RevealTextureBrowser = true;
			if (current.HasValue() && parameter.allowTextureFallback) {
				const auto remove = std::string(UiText("material.clear_image", "Remove texture")) + "###ClearTexture";
				clear = ImGui::Selectable(remove.c_str());
			}
			if (m_Project && ImGui::IsWindowAppearing())
				CollectSources(*m_Project, state, m_Error);
			for (const auto& entry : state.sources)
				if (entry.type == "image/color" && ImGui::Selectable(entry.label.c_str()))
					source = entry.path;
			ImGui::EndCombo();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload("CATALPA_SOURCE")) {
				const auto* data = static_cast<const char*>(payload->Data);
				if (data && payload->DataSize > 1 && data[payload->DataSize - 1] == 0)
					source = fs::u8path(data);
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::EndDisabled();
		if (clear) {
			current = {};
			return true;
		}
		if (source.empty() || busy)
			return false;
		if (!Tools::TextureImporter::SupportsMaterialMap(source)) {
			m_Error = UiText("material.image_types", "Choose a PNG, JPEG, TGA or BMP image.");
			return false;
		}
		if (!m_Project)
			return m_StandaloneTextures.Import(m_App.GetAssets(), source, semantic, current, m_Error);
		Tools::AssetBuildRequest request{*m_Project, source, state.shaders};
		auto* settings = state.build->ImportSettings(request, m_Error);
		if (!settings)
			return false;
		request.settings = *settings;
		request.settings.materialTextureViews |= semantic != ImageSemantic::Color;
		if (!state.build->Start(request, m_Error, m_Project->contentRoot))
			return false;
		settings->materialTextureViews = request.settings.materialTextureViews;
		state.document = doc.Id();
		state.source = source;
		state.generation = state.build->Generation();
		state.previousTexture = current;
		state.textureParameter = parameter.id;
		state.textureKey = "image/" + key;
		state.action = AuthorAssetAction::BindTexture;
		state.indexed = false;
		return false;
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
		else if (boundId == BuiltinAssetIds::CheckerGround)
			caption = UiText("assetwork.checkerboard", "Checkerboard");
		else if (boundId == BuiltinAssetIds::DefaultUnlitInstance)
			caption = UiText("assetwork.default_unlit", "Default unlit material");
		else if (boundId == BuiltinAssetIds::DefaultPbrInstance)
			caption = UiText("assetwork.default_pbr", "Default PBR material");
		else if (bound.HasValue() && bound.Get()->sceneLocal)
			caption = UiText("material.scene_local", "Scene material");
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
			if (ImGui::Selectable(UiText("assetwork.checkerboard", "Checkerboard"),
			        boundId == BuiltinAssetIds::CheckerGround))
				assign(BuiltinAssetIds::CheckerGround);
			if (ImGui::Selectable(UiText("assetwork.default_unlit", "Default unlit material"),
			        boundId == BuiltinAssetIds::DefaultUnlitInstance))
				assign(BuiltinAssetIds::DefaultUnlitInstance);
			if (ImGui::Selectable(UiText("assetwork.default_pbr", "Default PBR material"),
			        boundId == BuiltinAssetIds::DefaultPbrInstance))
				assign(BuiltinAssetIds::DefaultPbrInstance);
			if (!m_Project) {
				/* 会话场景也能直接编辑并保存这些内置材质。 */
				ImGui::TextWrapped("%s",
				    UiText("assetwork.session_materials",
				        "Editing a built-in material applies it to this slot. Save the scene to keep it."));
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

	bool WorkspaceEditing::CreateComponentSource(SceneDocument& doc, PersistentEntityId id, bool script, std::string_view name) try {
		VirtualPath checked;
		std::string diagnostic;
		if (name.empty() || name.front() == '.' || name.find('/') != name.npos ||
		    !VirtualPath::Parse("content:/" + std::string(name), checked, diagnostic)) {
			m_NewComponentError = UiText("component.invalid_filename", "Enter a valid file name without a folder.");
			return false;
		}
		auto filename = fs::u8path(name);
		const char* suffix = script ? ".cs" : ".behavior";
		if (Tools::AssetSourceExtension(filename) != suffix)
			filename += suffix;
		const auto* project = EnsureSessionAssets(true);
		if (!project) {
			m_NewComponentError = std::exchange(m_Error, {});
			return false;
		}
		const auto target = project->contentRoot / filename;
		if (fs::exists(target)) {
			m_NewComponentError = UiText("component.name_taken", "This file already exists. Choose another name.");
			return false;
		}
		const bool created = script ? CreateBehaviourFile(target, project->contentRoot, m_NewComponentError)
		                            : !CreateComponentGraphFile(target, project->contentRoot).empty();
		if (!created) {
			if (!script)
				m_NewComponentError = std::exchange(m_Error, {});
			return false;
		}
		m_AssetActions.indexed = false;
		m_AssetFilesChanged = true;
		const auto action = script ? AuthorAssetAction::BindScript : AuthorAssetAction::BindGraph;
		if (StartSourceAction(target, action, &doc, id) && script)
			EditExternalScript(target);
		/* 文件已创建。若构建失败，通过构建诊断和“选择”重试，避免反复创建文件。 */
		return true;
	} catch (const std::exception& failure) {
		m_NewComponentError = failure.what();
		return false;
	}

	void WorkspaceEditing::DrawBehaviorPicker(SceneDocument& doc, PersistentEntityId id, bool script, AssetId bound) {
		const auto* project = AssetProject();
		auto& state = m_AssetActions;
		const auto action = script ? AuthorAssetAction::BindScript : AuthorAssetAction::BindGraph;
		const auto type = script ? "script" : "behavior_graph";
		const auto* newLabel = script ? UiText("ui.new_csharp_script", "New C# script")
		                              : UiText("ui.new_behavior_graph", "New behavior graph");
		const auto* chooseLabel = script ? UiText("assetwork.choose_script", "Choose C# script...")
		                                 : UiText("assetwork.choose_graph", "Choose behavior graph...");
		const bool busy = !state.build || state.build->Busy() || state.action != AuthorAssetAction::None;
		ImGui::BeginDisabled(busy);
		const float width = ImGui::GetContentRegionAvail().x;
		bool create = ImGui::Button((std::string(newLabel) + "###NewComponentAsset").c_str());
		const auto& style = ImGui::GetStyle();
		if (ImGui::GetItemRectSize().x + style.ItemSpacing.x + ImGui::CalcTextSize(chooseLabel).x +
		        style.FramePadding.x * 2.f <= width)
			ImGui::SameLine();
		bool requested = m_Inspector.TakeBehaviorPicker(script);
		requested |= ImGui::Button(chooseLabel);
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload("CATALPA_SOURCE")) {
				const char* data = static_cast<const char*>(payload->Data);
				if (data && payload->DataSize > 1 && data[payload->DataSize - 1] == 0) {
					const auto path = fs::u8path(data);
					if (SourceType(path) == type && (!script || Tools::AssetSourceExtension(path) == ".cs"))
						StartSourceAction(path, action, &doc, id);
					else
						m_Error = "The dropped source is incompatible with this component";
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (project && ((bound.HasValue() && !state.indexed) || requested))
			CollectSources(*project, state, m_Error);
		if (requested) {
			state.filter = {};
			ImGui::OpenPopup("##SelectSource");
		}
		if (ImGui::BeginPopup("##SelectSource")) {
			ImGui::InputTextWithHint("##Filter", UiText("assetwork.filter", "Filter by name or path"),
			    state.filter.data(), state.filter.size());
			if (!m_Project && ImGui::Selectable(UiText("component.import_session", "Import into session..."))) {
				auto source = SelectComponentSource(project, type, m_Error);
				if (!source.empty())
					StartSourceAction(source, action, &doc, id);
			}
			create |= ImGui::Selectable(newLabel);
			for (const auto& row : state.sources)
				if (row.type == type && (!script || Tools::AssetSourceExtension(row.path) == ".cs") &&
				    row.label.find(state.filter.data()) != std::string::npos && ImGui::Selectable(row.label.c_str(), row.id == bound))
					StartSourceAction(row.path, action, &doc, id);
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
		if (create) {
			m_NewComponentName = {};
			m_NewComponentError.clear();
			m_NewComponentOwner = doc.Resolve()->MakeReference(doc.Resolve()->Resolve(id));
			ImGui::OpenPopup("##CreateComponentAsset");
		}
		if (ImGui::BeginPopup("##CreateComponentAsset")) {
			if (m_NewComponentOwner != doc.Resolve()->MakeReference(doc.Resolve()->Resolve(id))) {
				ImGui::CloseCurrentPopup();
			} else {
				ImGui::TextUnformatted(newLabel);
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				const bool enter = ImGui::InputTextWithHint("##AssetName", UiText("component.file_name", "File name"),
				    m_NewComponentName.data(), m_NewComponentName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::IsItemEdited())
					m_NewComponentError.clear();
				if (!m_NewComponentError.empty())
					ImGui::TextWrapped("%s", m_NewComponentError.c_str());
				const bool ready = !busy && m_NewComponentName[0] != '\0';
				ImGui::BeginDisabled(!ready);
				const bool confirm = ImGui::Button((std::string(UiText("component.create_attach", "Create and attach")) +
				    "###ConfirmCreate").c_str());
				ImGui::EndDisabled();
				if (ready && (enter || confirm) && CreateComponentSource(doc, id, script, m_NewComponentName.data()))
					ImGui::CloseCurrentPopup();
				ImGui::SameLine();
				if (ImGui::Button((std::string(UiText("ui.cancel", "Cancel")) + "###CancelCreate").c_str()))
					ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (!bound.HasValue())
			return;
		const auto it = std::find_if(state.sources.begin(), state.sources.end(),
		    [&](const auto& row) { return row.id == bound && row.type == type; });
		if (it == state.sources.end()) {
			ImGui::TextWrapped("%s", UiText("assetwork.missing_source", "Source unavailable"));
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
