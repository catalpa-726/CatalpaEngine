#include "Application/WorkspaceEditing.h"
#include "Core/Application.h"
#include "Core/PlatformServices.h"
#include "Documents/DocumentRecovery.h"
#include "Platform/AuthorFileDialogs.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabAsset.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Scripting/ScriptAsset.h"
#include "Core/Serialization/AuthorJson.h"
#include "MaterialGraph/MaterialGraphSource.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Commands/GraphCommands.h"
#include "Materials/MaterialValidation.h"
#include "Core/Serialization/SchemaMigrationRegistry.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Assets/AssetManifest.h"
#include "Assets/AssetDependencyGraph.h"
#include "Panels/EditorText.h"
#include "imgui.h"
#include "Viewport/ToolbarButton.h"

namespace Catalpa::Editor {
	void WorkspaceEditing::SetProject(const Tools::ProjectConfig* config) {
		if (!config && !m_Project)
			return;
		if (config && m_Project && config->id == m_Project->id && config->projectFile == m_Project->projectFile &&
		    config->cookedRoot == m_Project->cookedRoot) {
			m_Project = *config;
			return;
		}
		m_Project = config ? std::optional(*config) : std::nullopt;
		m_Recovery.clear();
		m_RecoveryRoot = config ? config->projectFile.parent_path() / "Recovery" : std::filesystem::path{};
		if (!config)
			return;
		(void)LoadProjectCatalog();
		std::error_code error;
		for (std::filesystem::directory_iterator it(m_RecoveryRoot, error), end; !error && it != end;
		    it.increment(error)) {
			if (m_Recovery.size() == 128)
				break;
			if (it->is_regular_file(error) && it->path().filename().wstring().ends_with(L".recovery.json"))
				m_Recovery.push_back(it->path());
		}
	}

	namespace {
		bool InstallDirectoryCatalog(Application& app, const std::filesystem::path& root, bool optional,
		    std::string& error, AssetDependencyGraph* graph = nullptr) {
			if (optional && !std::filesystem::exists(root / "Assets.manifest"))
				return true;
			VirtualFileSystem files;
			VirtualPath locator;
			std::vector<std::byte> bytes;
			AssetRegistry catalog;
			if (!files.MountReadOnly("project-cooked", root, 0, error) ||
			    !VirtualPath::Parse("project-cooked:/Assets.manifest", locator, error) ||
			    !files.ReadFile(locator, 64 * 1024 * 1024, bytes, error) || !AssetManifest::Read(bytes, catalog, error))
				return false;
			if (catalog.GetRecords().empty() && optional)
				return true;
			if (graph && !graph->Build(catalog.GetRecords(), error))
				return false;
			/* 只安装目录，不在 UI 一次性解码项目所有资产；场景或组件按需请求依赖。 */
			return app.GetAssets().InstallCookedCatalog(root, catalog, error, true);
		}
	}

	bool WorkspaceEditing::LoadProjectCatalog() try {
		return InstallDirectoryCatalog(m_App, m_Project->cookedRoot, true, m_Error);
	} catch (const std::exception& exception) {
		m_Error = exception.what();
		return false;
	}

	void WorkspaceEditing::OpenGroveShowcase() try {
		if (CanReturnFromGrove() || m_PlayDocument.HasValue())
			return;
		const SceneHandle previous = m_App.GetScenes().GetActiveHandle();
		if (!previous.HasValue()) {
			m_Error = "No active scene to return to";
			return;
		}
		std::filesystem::path directory;
		if (!GetExecutableDirectory(directory, m_Error))
			return;
		const auto root = directory / "Resources" / "Grove";
		AssetDependencyGraph graph;
		if (!InstallDirectoryCatalog(m_App, root / "Cooked", false, m_Error, &graph))
			return;
		const auto path = std::filesystem::canonical(root / "Grove.scene.json");
		/* 复用作者场景的异步加载。已有文档及未保存修改继续由原文档会话持有。 */
		if (!OpenAuthor(path, {}, {}, {}, &graph))
			return;
		m_GrovePath = path;
		m_PreGroveScene = previous;
		m_ResetCamera = true;
	} catch (const std::exception& exception) {
		m_Error = exception.what();
	}

	void WorkspaceEditing::ReturnFromGrove() {
		if (!CanReturnFromGrove() || !StopPlay())
			return;
		const auto previous = m_PreGroveScene;
		const auto target = m_App.GetScenes().GetScene(previous) ? previous : m_HostScene;
		if (const auto status = m_App.GetScenes().SetActive(target); !status.IsSuccess()) {
			m_Error = status.message;
			return;
		}
		CancelPendingOpen();
		if (auto* doc = m_Documents.SceneFor(m_App.GetScenes(), target, m_Error))
			m_Documents.Activate(doc->Id());
		m_PreGroveScene = {};
		m_GrovePath.clear();
		m_ResetCamera = true;
	}

	/**
	 * @brief 保存文档。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] saveAs 为 true 时提示另选保存路径，为 false 时优先沿用文档路径。
	 *
	 * @return 完成上述操作时为 true，操作未成功时为 false。
	 */
	bool WorkspaceEditing::SaveDocument(Document& doc, bool saveAs) {
		if (!m_Project || m_PlayDocument.HasValue()) {
			m_Error = "Saving requires an author project outside Play";
			return false;
		}
		auto target = doc.AuthorPath();
		const auto type = doc.TypeKey();
		std::string suffix(DocumentRecovery::AuthorSuffix(type));
		if (suffix.empty()) {
			m_Error = "Original audio, font and image assets are read-only; save an author document instead";
			return false;
		}
		if (type == "script") {
			const auto* source = dynamic_cast<const AssetDocument*>(&doc);
			const auto first =
			    source && source->Source() ? source->Source()->find_first_not_of(" \t\r\n") : std::string::npos;
			suffix = first != std::string::npos && (*source->Source())[first] == '{' ? "script" : "lua";
		}
		if (saveAs || target.empty()) {
			bool cancelled = false;
			if (!SelectAuthorFile(true, m_Project->contentRoot, target, cancelled, m_Error, suffix) || cancelled)
				return false;
			if (target.extension().empty()) {
				target += std::filesystem::u8path("." + suffix);
			}
		}
		if (!m_Documents.CanSaveTo(doc, target, m_Error) ||
		    !DocumentRecovery::Save(doc, target, m_Project->contentRoot, m_Error))
			return false;
		RemoveRecovery(doc.Id());
		return true;
	}

	void WorkspaceEditing::RetainRecovery(const std::filesystem::path& path) {
		if (!path.empty() && std::find(m_Recovery.begin(), m_Recovery.end(), path) == m_Recovery.end())
			m_Recovery.push_back(path);
	}

	void WorkspaceEditing::CancelPendingOpen() {
		/* 取消的是本次打开意图，不删除恢复文件；失败或改选后仍能再次恢复。 */
		if (m_PendingScene && m_PendingScene->path == m_GrovePath) {
			m_PreGroveScene = {};
			m_GrovePath.clear();
		}
		if (m_PendingScene)
			RetainRecovery(m_PendingScene->recovery);
		if (m_PendingAuthor)
			RetainRecovery(m_PendingAuthor->recovery);
		m_PendingScene.reset();
		m_PendingAuthor.reset();
	}

	void WorkspaceEditing::RemoveRecovery(StableId id) {
		const auto remove = [&](const std::filesystem::path& path) {
			std::error_code error;
			std::filesystem::remove(path, error);
			if (error)
				m_Error = "Author data is saved/closed, but its recovery copy remains: " + error.message();
			return !error;
		};
		if (!m_RecoveryRoot.empty())
			(void)remove(m_RecoveryRoot / (id.ToString() + ".recovery.json"));
		const auto restored = m_RestoredRecovery.find(id);
		if (restored != m_RestoredRecovery.end() && remove(restored->second))
			m_RestoredRecovery.erase(restored);
	}

	bool WorkspaceEditing::SaveAll() {
		if (m_PlayDocument.HasValue() && !StopPlay())
			return false;
		for (auto* doc : m_Documents.All()) {
			if (doc->History().Dirty()) {
				if (!SaveDocument(*doc))
					return false;
			} else
				RemoveRecovery(doc->Id());
		}
		return true;
	}

	void WorkspaceEditing::DiscardRecoveryCopies() {
		for (auto* doc : m_Documents.All())
			RemoveRecovery(doc->Id());
	}

	/**
	 * @brief 从源文件或恢复副本打开作者文档，并按文档类型处理依赖。
	 *
	 * @param[in] requestedPath 要打开的作者文件路径，恢复未命名文档时可为空。
	 * @param[in] source 恢复副本或调用方提供的作者文本；常规打开时从文件读取。
	 * @param[in] type 作者文档类型键；空值表示按扩展名和内容识别。
	 * @param[in] recovery 对应恢复副本的路径；正常打开时为空。
	 * @param[in] loadGraph 可选的资产依赖图，用于按依赖顺序安排作者场景所需资产的加载。
	 *
	 * @return 完成上述操作时为 true，操作未成功时为 false。
	 */
	bool WorkspaceEditing::OpenAuthor(const std::filesystem::path& requestedPath, std::string source, std::string type,
	    const std::filesystem::path& recovery, const AssetDependencyGraph* loadGraph) try {
		if (m_PlayDocument.HasValue()) {
			m_Error = "Stop Play before opening author data";
			return false;
		}
		const auto path = requestedPath.empty() ? std::filesystem::path{} : std::filesystem::canonical(requestedPath);
		CancelPendingOpen();
		const auto ext = Tools::AssetSourceExtension(path);
		if (source.empty() && type.empty() &&
		    !DocumentRecovery::Read(path, source, m_Error,
		        ext == ".animclip" || ext == ".animation" || ext == ".skin" ? DocumentRecovery::MaxAnimationBytes
		                                                                    : DocumentRecovery::MaxAuthorBytes))
			return false;
		if (type.empty()) {
			type = DocumentRecovery::AuthorType(ext, source);
			if (type.empty()) {
				m_Error = "Choose a supported author document; original models, textures, audio and fonts "
				          "use Content Browser import/preview";
				return false;
			}
		}
		if (DocumentRecovery::AuthorSuffix(type).empty() || source.size() > DocumentRecovery::AuthorLimit(type)) {
			m_Error = "Unsupported author domain or source byte budget exceeded";
			return false;
		}
		if (auto* existing = m_Documents.FindByPath(path, m_Error)) {
			m_Documents.Activate(existing->Id());
			if (auto* scene = dynamic_cast<SceneDocument*>(existing)) {
				const auto status = m_App.GetScenes().SetActive(scene->Handle());
				if (!status.IsSuccess()) {
					m_Error = status.message;
					return false;
				}
			} else
				m_OpenAsset = existing->Id();
			return true;
		}
		if (!m_Error.empty())
			return false;
		if (type == "scene") {
			SceneAsset asset;
			SchemaMigrationRegistry migrations;
			if (!SceneSerializer::ReadMigrated(source, migrations, asset, m_Error))
				return false;
			auto dependencies = SceneSerializer::Dependencies(asset);
			if (loadGraph) {
				/* 只加载本场景可达的记录，着色器/纹理先于材质到达解码队列。 */
				std::set<AssetId> needed;
				for (size_t i = 0; i < dependencies.size(); ++i) {
					if (!needed.insert(dependencies[i]).second)
						continue;
					auto input = loadGraph->Dependencies(dependencies[i]);
					dependencies.insert(dependencies.end(), input.begin(), input.end());
				}
				dependencies.clear();
				for (auto id : loadGraph->Order())
					if (needed.erase(id))
						dependencies.push_back(id);
				if (!needed.empty()) {
					m_Error = "Grove scene references a missing cooked asset";
					return false;
				}
			}
			const bool pending = std::any_of(dependencies.begin(), dependencies.end(),
			    [&](auto id) { return m_App.GetAssets().GetStatus(id) != AssetStatus::Ready; });
			if (pending) {
				m_PendingScene = PendingScene{std::move(asset), path, recovery, std::move(dependencies)};
				m_Error = "Loading scene dependencies; current scene retained.";
				return true;
			}
			if (!InstallAuthorScene(asset, path, recovery))
				return false;
		} else if (type == "material_instance") {
			const auto data = AuthorJson::Parse(source);
			const auto definition = AuthorJson::Identity<AssetId>(data.at("definitionId"));
			const auto waitFor = [&](std::vector<AssetId> ids) {
				bool pending = false;
				for (auto id : ids) {
					auto status = m_App.GetAssets().GetStatus(id);
					if (status == AssetStatus::Unloaded) {
						if (!m_App.GetAssets().Request(id, m_Error))
							return -1;
						status = m_App.GetAssets().GetStatus(id);
					}
					if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
						m_Error = m_App.GetAssets().GetDiagnostic(id);
						return -1;
					}
					pending |= status != AssetStatus::Ready;
				}
				if (pending)
					m_PendingAuthor = PendingAuthor{path, recovery, source, type, std::move(ids)};
				return pending ? 1 : 0;
			};
			const auto loadingDefinition = waitFor({definition});
			if (loadingDefinition != 0)
				return loadingDefinition > 0;
			AssetVersionRef<MaterialDefinition> fixed;
			bool fallback = false;
			if (!m_App.GetAssets().Acquire(definition, fixed, fallback, m_Error) || fallback)
				return false;
			std::shared_ptr<const MaterialInstance> material;
			if (!ParseMaterialInstance(source, fixed, material, m_Error))
				return false;
			std::vector<AssetId> textures;
			for (const auto& field : material->parameters)
				if (const auto* image = std::get_if<AssetHandle<ImageAsset>>(&field.value); image && image->HasValue())
					textures.push_back(image->id);
			const auto loadingTextures = waitFor(std::move(textures));
			if (loadingTextures != 0)
				return loadingTextures > 0;
			auto resolved = std::make_shared<MaterialInstance>(*material);
			constexpr std::array<std::string_view, 5> names{"baseColor_texture", "normal_texture",
			    "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
			for (const auto& field : material->parameters) {
				const auto* image = std::get_if<AssetHandle<ImageAsset>>(&field.value);
				if (!image || !image->HasValue())
					continue;
				const auto slot = std::find(names.begin(), names.end(), field.parameterId);
				const auto schema = std::find_if(fixed.Get()->parameters.begin(), fixed.Get()->parameters.end(),
				    [&](const auto& parameter) { return parameter.id == field.parameterId; });
				AssetVersionRef<ImageAsset> version;
				if (schema == fixed.Get()->parameters.end() ||
				    !m_App.GetAssets().Acquire(image->id, version, fallback, m_Error) || fallback ||
				    !ValidateMaterialTexture(*schema, *version.Get(), m_Error))
					return false;
				if (slot != names.end())
					resolved->textureVersions[size_t(slot - names.begin())] = std::move(version);
			}
			material = std::move(resolved);
			/* 恢复副本没有作者路径，每份草稿必须有独立身份，不能因相同 revision 合并文档。 */
			AssetId identity;
			if (path.empty()) {
				StableId generated;
				if (!GenerateStableId(generated, m_Error) || !AssetId::TryParse(generated.ToString(), identity))
					return false;
			} else
				identity = Tools::AssetBuildIdentity("material-author:" + Tools::AssetPathText(path));
			AssetVersionRef<MaterialInstance> version;
			if (!AssetVersionRef<MaterialInstance>::TryCreate(identity, material->revision, material, version, m_Error))
				return false;
			auto* doc = m_Documents.Open(version, m_Error);
			if (!doc)
				return false;
			doc->SetAuthorPath(path);
			m_OpenAsset = doc->Id();
			ActivateAsset(*doc);
		} else {
			if (recovery.empty()) {
				if (!DocumentRecovery::ValidateAuthorSource(type, source, m_Error))
					return false;
				if (type == "prefab") {
					PrefabAsset checked;
					if (!ReadPrefab(source, checked, m_Error))
						return false;
				}
				if (type == "behavior_graph") {
					BehaviorGraphAsset checked;
					if (!ReadBehaviorGraph(source, checked, m_Error))
						return false;
				}
				if (type == "material_graph") {
					Tools::MaterialGraphSource checked;
					if (!Tools::ReadMaterialGraph(source, checked, m_Error))
						return false;
				}
				if (type == "material_definition") {
					std::shared_ptr<const MaterialDefinition> checked;
					if (!ParseMaterialDefinition(source, checked, m_Error))
						return false;
				}
			}
			AssetId id;
			StableId identity;
			if (path.empty()) {
				if (!GenerateStableId(identity, m_Error) || !AssetId::TryParse(identity.ToString(), id))
					return false;
			} else
				id = Tools::AssetBuildIdentity("author:" + Tools::AssetPathText(path));
			auto* doc = m_Documents.OpenSource(id, type, std::move(source), path, m_Error);
			if (!doc)
				return false;
			m_OpenAsset = doc->Id();
			ActivateAsset(*doc);
		}
		if (!recovery.empty() && type != "scene") {
			if (auto* doc = m_Documents.Active()) {
				m_RestoredRecovery[doc->Id()] = recovery;
				doc->History().MarkUnsaved();
			}
		}
		m_Error.clear();
		return true;
	} catch (const std::exception& exception) {
		m_Error = exception.what();
		return false;
	}

	/**
	 * @brief 实例化作者场景、建立编辑文档并激活该场景。
	 *
	 * @param[in] asset 已反序列化并准备实例化的作者场景。
	 * @param[in] path 文件系统中的目标路径。
	 * @param[in] recovery 对应恢复副本的路径；正常打开时为空。
	 *
	 * @return 场景已实例化、编辑文档已登记并激活时为 true；失败时回收候选场景。
	 */
	bool WorkspaceEditing::InstallAuthorScene(
	    const SceneAsset& asset, const std::filesystem::path& path, const std::filesystem::path& recovery) try {
		auto authorPath = path;
		m_AuthorScenes.reserve(m_AuthorScenes.size() + 1);
		SceneHandle candidate;
		if (!SceneSerializer::Instantiate(asset, m_App.GetScenes(), &m_App.GetAssets(), candidate, m_Error))
			return false;
		struct Candidate {
			Application& app;
			DocumentManager& documents;
			SceneHandle scene;
			StableId document;
			bool committed = false;

			~Candidate() {
				if (!committed) {
					if (document.HasValue())
						documents.Close(document);
					(void)app.GetScenes().Unload(scene);
				}
			}
		} guard{m_App, m_Documents, candidate};
		auto* doc = m_Documents.SceneFor(m_App.GetScenes(), candidate, m_Error);
		if (!doc)
			return false;
		guard.document = doc->Id();
		doc->SetAuthorPath(std::move(authorPath));
		if (!recovery.empty()) {
			m_RestoredRecovery[doc->Id()] = recovery;
			doc->History().MarkUnsaved();
		}
		const auto status = m_App.GetScenes().SetActive(candidate);
		if (!status.IsSuccess()) {
			m_RestoredRecovery.erase(doc->Id());
			m_Error = status.message;
			return false;
		}
		m_AuthorScenes.push_back(candidate);
		m_Documents.Activate(doc->Id());
		m_ResetCamera = true;
		guard.committed = true;
		m_Error.clear();
		return true;
	} catch (const std::exception& exception) {
		m_Error = exception.what();
		return false;
	}

	void WorkspaceEditing::CloseDocument(StableId id) {
		auto* doc = m_Documents.Find(id);
		if (!doc || m_PlayDocument.HasValue())
			return;
		CancelPendingOpen();
		if (auto* scene = dynamic_cast<SceneDocument*>(doc)) {
			m_Picking.Cancel(m_App);
			m_Gizmo.Cancel(m_Error);
			m_App.CloseEditorPreview(0x43415453434e4501ULL);
			const auto status = m_App.GetScenes().Unload(scene->Handle());
			if (!status.IsSuccess()) {
				m_Error = status.message;
				return;
			}
			std::erase(m_AuthorScenes, scene->Handle());
		} else {
			m_AssetEditors.Close(m_App);
			if (m_OpenAsset == id)
				m_OpenAsset = {};
		}
		RemoveRecovery(id);
		m_Documents.Close(id);
		m_Closing = {};
	}

	/**
	 * @brief 绘制文档切换和管理菜单。
	 */
	void WorkspaceEditing::DocumentMenu() {
		if (!ImGui::BeginMenu(UiLabel("ui.documents", "Documents").c_str()))
			return;
		ImGui::BeginDisabled(!m_Project || m_PlayDocument.HasValue());
		if (ImGui::MenuItem(UiLabel("ui.new_scene", "New scene").c_str())) {
			CancelPendingOpen();
			SceneAsset scene;
			scene.name = "Untitled scene";
			if (InstallAuthorScene(scene, {}))
				m_Documents.Active()->History().MarkUnsaved();
		}
		const auto create = [&](const char* label, std::string_view type, std::string source) {
			if (!ImGui::MenuItem(label))
				return;
			CancelPendingOpen();
			AssetId id;
			std::string ignored;
			StableId random;
			if (!GenerateStableId(random, m_Error) || !AssetId::TryParse(random.ToString(), id))
				return;
			auto* doc = m_Documents.OpenSource(id, std::string(type), std::move(source), {}, m_Error);
			if (doc) {
				doc->History().MarkUnsaved();
				m_OpenAsset = doc->Id();
				ActivateAsset(*doc);
			}
		};
		create(UiLabel("ui.new_lua_script", "New Lua script").c_str(), "script",
		    "return {\n    update = function(dt)\n        scene.translate(0, dt, 0)\n    end\n}\n");
		std::string graphText;
		Tools::MaterialGraphSource material = Tools::DefaultMaterialGraph();
		if (Tools::WriteMaterialGraph(material, graphText, m_Error))
			create(UiLabel("ui.new_material_graph", "New material graph").c_str(), "material_graph", graphText);
		BehaviorGraphAsset behavior;
		StableId entry;
		std::string ignored;
		if (GenerateStableId(entry, ignored)) {
			BehaviorNode node;
			node.id = entry;
			node.type = "Update";
			behavior.nodes.push_back(node);
			if (WriteBehaviorGraph(behavior, graphText, ignored))
				create(UiLabel("ui.new_behavior_graph", "New behavior graph").c_str(), "behavior_graph", graphText);
		}
		if (ImGui::MenuItem(UiLabel("ui.open_author_document", "Open author document...").c_str())) {
			std::filesystem::path file;
			bool cancelled = false;
			if (SelectAuthorFile(false, m_Project->contentRoot, file, cancelled, m_Error) && !cancelled)
				OpenAuthor(file);
		}
		auto* active = m_Documents.Active();
		if (ImGui::MenuItem(UiLabel("ui.save", "Save").c_str(), nullptr, false, active != nullptr))
			SaveDocument(*active);
		if (ImGui::MenuItem(UiLabel("ui.save_as", "Save as...").c_str(), nullptr, false, active != nullptr))
			SaveDocument(*active, true);
		if (ImGui::MenuItem(UiLabel("ui.save_all", "Save all").c_str()))
			SaveAll();
		PrefabMenu();
		if (ImGui::MenuItem(UiLabel("ui.close", "Close").c_str(), nullptr, false, active != nullptr)) {
			if (active->History().Dirty())
				m_Closing = active->Id();
			else
				CloseDocument(active->Id());
		}
		ImGui::EndDisabled();
		ImGui::Separator();
		for (auto* doc : m_Documents.All()) {
			auto label = (doc->AuthorPath().empty() ? std::string(doc->TypeKey())
			                                        : Tools::AssetPathText(doc->AuthorPath().filename()));
			if (doc->History().Dirty())
				label += " *";
			label += "###" + doc->Id().ToString();
			if (ImGui::MenuItem(label.c_str(), nullptr, doc == m_Documents.Active(), !m_PlayDocument.HasValue())) {
				CancelPendingOpen();
				m_Documents.Activate(doc->Id());
				if (auto* scene = dynamic_cast<SceneDocument*>(doc))
					(void)m_App.GetScenes().SetActive(scene->Handle());
				else
					m_OpenAsset = doc->Id();
			}
		}
		ImGui::EndMenu();
	}

	/**
	 * @brief 更新文档。
	 */
	void WorkspaceEditing::UpdateDocuments() {
		if (m_PendingAuthor && !m_PlayDocument.HasValue()) {
			bool ready = true, failed = false;
			for (auto id : m_PendingAuthor->dependencies) {
				const auto status = m_App.GetAssets().GetStatus(id);
				if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
					m_Error = m_App.GetAssets().GetDiagnostic(id);
					failed = true;
					break;
				}
				ready &= status == AssetStatus::Ready;
			}
			if (failed)
				CancelPendingOpen();
			else if (ready) {
				auto value = std::move(*m_PendingAuthor);
				m_PendingAuthor.reset();
				if (!OpenAuthor(value.path, std::move(value.source), std::move(value.type), value.recovery))
					RetainRecovery(value.recovery);
			}
		}
		if (m_PendingScene && !m_PlayDocument.HasValue()) {
			/* 每次只准入一个未加载依赖，复用已经发布的版本。等待期间保留当前场景。 */
			auto& assets = m_App.GetAssets();
			auto& pending = *m_PendingScene;
			while (pending.cursor < pending.dependencies.size() &&
			    assets.GetStatus(pending.dependencies[pending.cursor]) == AssetStatus::Ready)
				++pending.cursor;
			if (pending.cursor < pending.dependencies.size()) {
				const auto id = pending.dependencies[pending.cursor];
				const auto status = assets.GetStatus(id);
				if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
					m_Error = assets.GetDiagnostic(id);
					CancelPendingOpen();
				} else if (status == AssetStatus::Unloaded && assets.GetPendingCount() == 0 &&
				    !assets.Request(id, m_Error)) {
					CancelPendingOpen();
				}
			} else {
				auto candidate = std::move(*m_PendingScene);
				m_PendingScene.reset();
				if (!InstallAuthorScene(candidate.scene, candidate.path, candidate.recovery)) {
					if (candidate.path == m_GrovePath) {
						m_PreGroveScene = {};
						m_GrovePath.clear();
					}
					RetainRecovery(candidate.recovery);
				}
			}
		}
		if (m_Closing.HasValue())
			ImGui::OpenPopup(UiLabel("ui.unsaved_document", "Unsaved document").c_str());
		if (ImGui::BeginPopupModal(UiLabel("ui.unsaved_document", "Unsaved document").c_str(), nullptr,
		        ImGuiWindowFlags_AlwaysAutoResize)) {
			auto* doc = m_Documents.Find(m_Closing);
			ImGui::TextUnformatted(UiText("ui.save_changes_before_closing", "Save changes before closing?"));
			if (ImGui::Button(UiLabel("ui.save", "Save").c_str()) && doc && SaveDocument(*doc)) {
				CloseDocument(m_Closing);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(UiLabel("ui.discard", "Discard").c_str())) {
				CloseDocument(m_Closing);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(UiLabel("ui.cancel", "Cancel").c_str())) {
				m_Closing = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		const auto now = std::chrono::steady_clock::now();
		if (m_Project && now - m_LastRecovery > std::chrono::seconds(30)) {
			m_LastRecovery = now;
			for (auto* doc : m_Documents.All())
				if (doc->History().Dirty())
					(void)DocumentRecovery::Checkpoint(*doc, m_RecoveryRoot, m_Error);
		}
		if (m_Recovery.empty())
			return;
		ImGui::OpenPopup(UiLabel("ui.recovered_author_document", "Recovered author document").c_str());
		if (ImGui::BeginPopupModal(UiLabel("ui.recovered_author_document", "Recovered author document").c_str(),
		        nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped(
			    UiFormat("ui.a_separate_recovery_copy_is_available", "A separate recovery copy is available: %s"),
			    Tools::AssetPathText(m_Recovery.back()).c_str());
			if (ImGui::Button(UiLabel("ui.restore_as_unsaved_document", "Restore as unsaved document").c_str())) {
				const auto recovery = m_Recovery.back();
				std::string text;
				if (DocumentRecovery::Read(recovery, text, m_Error, DocumentRecovery::MaxRecoveryBytes))
					try {
						std::string type, content;
						if (!DocumentRecovery::DecodeCheckpoint(text, type, content, m_Error))
							throw std::invalid_argument(m_Error);
						/* OpenAuthor 可能把上一次等待中的恢复项重新入队，不能借用 vector 内的路径。 */
						m_Recovery.pop_back();
						if (OpenAuthor({}, std::move(content), type, recovery)) {
							ImGui::CloseCurrentPopup();
						} else
							RetainRecovery(recovery);
					} catch (const std::exception& exception) {
						m_Error = exception.what();
					}
			}
			ImGui::SameLine();
			if (ImGui::Button(UiLabel("ui.discard_recovery", "Discard recovery").c_str())) {
				std::error_code error;
				std::filesystem::remove(m_Recovery.back(), error);
				if (error)
					m_Error = error.message();
				else {
					m_Recovery.pop_back();
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}

	bool WorkspaceEditing::StopPlay() {
		auto* doc = dynamic_cast<SceneDocument*>(m_Documents.Find(m_PlayDocument));
		if (!doc) {
			m_PlayDocument = {};
			return true;
		}
		m_Picking.Cancel(m_App);
		m_App.CloseEditorPreview(0x43415453434e4501ULL);
		if (!m_App.SetEditorPlayScene({}, false, m_Error) || !doc->StopPlay(m_Error))
			return false;
		m_PlayDocument = {};
		m_PlayPaused = false;
		return true;
	}

	void WorkspaceEditing::PlayToolbar(const PanelText& text) {
		auto* doc = ActiveScene();
		const bool playing = doc && doc->Playing();
		ImGui::BeginDisabled(!doc || playing);
		if (ToolbarButton("##play", ToolIcon::Play, text("play.start", "Play", nullptr))) {
			CancelPendingOpen();
			m_Gizmo.Cancel(m_Error);
			m_Picking.Cancel(m_App);
			if (doc->StartPlay(m_App.GetAssets(), m_Error)) {
				if (m_App.SetEditorPlayScene(doc->PlayHandle(), false, m_Error))
					m_PlayDocument = doc->Id();
				else {
					std::string ignored;
					doc->StopPlay(ignored);
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!playing);
		if (ToolbarButton("##pause", ToolIcon::Pause,
		        text(m_PlayPaused ? "play.resume" : "play.pause", m_PlayPaused ? "Resume" : "Pause", nullptr),
		        m_PlayPaused)) {
			if (m_App.SetEditorPlayScene(doc->PlayHandle(), !m_PlayPaused, m_Error))
				m_PlayPaused = !m_PlayPaused;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_PlayPaused);
		if (ToolbarButton("##step", ToolIcon::Step, text("play.step", "Step one tick", nullptr)))
			(void)m_App.StepEditorPlay(m_Error);
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ToolbarButton("##stop", ToolIcon::Stop, text("play.stop", "Stop", nullptr)))
			StopPlay();
		ImGui::EndDisabled();
	}
}
