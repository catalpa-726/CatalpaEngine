#include "AssetEditors/MaterialGraphEditor.h"

#include "Core/Application.h"
#include "Core/PlatformServices.h"
#include "Core/Jobs/JobSystem.h"
#include "Commands/GraphCommands.h"
#include "Assets/AssetManager.h"
#include "Materials/MaterialValidation.h"
#include "Materials/MaterialAssetLoader.h"
#include "Shaders/ShaderLibrary.h"
#include "Panels/EditorText.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		bool FloatFields(const char* label, std::vector<float>& value) {
			return ImGui::InputScalarN(label, ImGuiDataType_Float, value.data(), int(value.size()), nullptr, nullptr,
			    "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
		}

		AssetId RandomAsset(std::string& error) {
			StableId id;
			AssetId result;
			if (GenerateStableId(id, error))
				(void)AssetId::TryParse(id.ToString(), result);
			return result;
		}
	}

	bool MaterialGraphEditor::Commit(AssetDocument& doc, const Tools::MaterialGraphSource& graph, std::string& error) {
		std::string source;
		return Tools::WriteMaterialGraph(graph, source, error) &&
		    (source == *doc.Source() || ChangeAuthorSource(doc, std::move(source), error));
	}

	/**
	 * @brief 绘制场景编辑工具的选择控件。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] graph 作为添加节点及其他工具栏操作起点的当前材质图。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 */
	void MaterialGraphEditor::EditToolbar(
	    AssetDocument& doc, Tools::MaterialGraphSource& graph, const PanelText& text, std::string& error) {
		ImGui::SliderInt(text("graph.node_width", "New numeric node width", "GraphNodeWidth").c_str(), &m_Width, 1, 4);
		ImGui::InputText(
		    text("graph.texture_id", "Texture asset ID", "GraphTextureId").c_str(), m_Texture.data(), m_Texture.size());
		ImGui::Combo(text("graph.texture_role", "Texture role", "GraphTextureRole").c_str(), &m_Semantic,
		    UiChoices({UiText("choice.basecolor", "baseColor"), UiText("choice.normal", "normal"),
		                  UiText("choice.metallicroughness", "metallicRoughness"),
		                  UiText("choice.emissive", "emissive"), UiText("choice.occlusion", "occlusion")})
		        .c_str());
		if (ImGui::BeginCombo(text("graph.add_node", "Add node", "GraphAddNode").c_str(),
		        text("graph.choose_node", "Choose node type", nullptr).c_str())) {
			for (const auto type : Tools::MaterialNodeTypes()) {
				if (type == "SurfaceOutput")
					continue;
				if (ImGui::Selectable((GraphTerm(type) + "###" + std::string(type)).c_str())) {
					auto next = graph;
					StableId id;
					if (Tools::AddMaterialNode(next, type, uint32_t(m_Width), id, error)) {
						if (type == "TextureSample") {
							constexpr const char* names[]{
							    "baseColor", "normal", "metallicRoughness", "emissive", "occlusion"};
							next.nodes.back().semantic = names[m_Semantic];
							if (!AssetId::TryParse(m_Texture.data(), next.nodes.back().texture))
								error = "Enter the cooked image asset ID before adding a texture node";
							else if (Commit(doc, next, error)) {
								graph = std::move(next);
								m_Selected = id;
							}
						} else if (Commit(doc, next, error)) {
							graph = std::move(next);
							m_Selected = id;
						}
					}
				}
			}
			ImGui::EndCombo();
		}
		int model = graph.shadingModel == "pbr" ? 0 : 1;
		int alpha = graph.alphaMode == "opaque" ? 0 : graph.alphaMode == "mask" ? 1 : 2;
		bool changed = false;
		if (ImGui::Combo(text("graph.shading", "Shading", "GraphShading").c_str(), &model,
		        UiChoices({UiText("choice.pbr", "PBR"), UiText("choice.unlit", "Unlit")}).c_str())) {
			graph.shadingModel = model ? "unlit" : "pbr";
			changed = true;
		}
		const std::array<std::string, 3> alphaNames{text("graph.alpha.opaque", "Opaque", nullptr),
		    text("graph.alpha.mask", "Masked", nullptr), text("graph.alpha.blend", "Blend", nullptr)};
		const char* alphaLabels[]{alphaNames[0].c_str(), alphaNames[1].c_str(), alphaNames[2].c_str()};
		if (ImGui::Combo(text("graph.alpha_mode", "Alpha mode", "GraphAlphaMode").c_str(), &alpha, alphaLabels, 3)) {
			graph.alphaMode = alpha == 0 ? "opaque" : alpha == 1 ? "mask" : "blend";
			changed = true;
		}
		if (changed)
			Commit(doc, graph, error);
	}

	/**
	 * @brief 编辑画布。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] graph 提供材质节点、端口、连线和布局的图数据。
	 * @param[out] error 失败时接收原因说明。
	 */
	void MaterialGraphEditor::EditCanvas(AssetDocument& doc, Tools::MaterialGraphSource& graph, std::string& error) {
		std::vector<CanvasNode> nodes;
		std::vector<CanvasLink> links;
		for (const auto& n : graph.nodes) {
			CanvasNode node;
			node.id = n.id;
			node.title = GraphTerm(n.type);
			if (const auto p = graph.layout.find(n.id); p != graph.layout.end()) {
				node.x = p->second.x;
				node.y = p->second.y;
			}
			for (const auto& port : Tools::MaterialNodePorts(n))
				node.pins.push_back({n.ports.at(port.name), n.id, GraphTerm(port.name), port.input});
			nodes.push_back(std::move(node));
		}
		for (const auto& l : graph.links)
			links.push_back({l.id, l.from.port, l.to.port});
		const auto connect = [&](const CanvasPin& from, const CanvasPin& to, std::string& reason) {
			auto next = graph;
			std::erase_if(next.links, [&](const auto& l) { return l.to.port == to.id; });
			StableId id;
			if (!GenerateStableId(id, reason))
				return false;
			next.links.push_back({id, {from.node, from.id}, {to.node, to.id}});
			return Tools::ValidateMaterialGraph(next, reason);
		};
		const auto edits = m_Canvas.Draw(doc.Id(), nodes, links, connect, error);
		if (!error.empty())
			m_Diagnostic = error;
		if (edits.selected.HasValue())
			m_Selected = edits.selected;
		auto next = graph;
		if (edits.connection) {
			const auto& [from, to] = *edits.connection;
			StableId id;
			if (!GenerateStableId(id, error))
				return;
			std::erase_if(next.links, [&](const auto& l) { return l.to.port == to.id; });
			next.links.push_back({id, {from.node, from.id}, {to.node, to.id}});
		}
		for (auto id : edits.removeLinks)
			std::erase_if(next.links, [&](const auto& l) { return l.id == id; });
		for (auto id : edits.removeNodes) {
			const auto found =
			    std::find_if(next.nodes.begin(), next.nodes.end(), [&](const auto& n) { return n.id == id; });
			if (found != next.nodes.end() && found->type == "SurfaceOutput")
				continue;
			std::erase_if(next.nodes, [&](const auto& n) { return n.id == id; });
			std::erase_if(next.links, [&](const auto& l) { return l.from.node == id || l.to.node == id; });
			next.layout.erase(id);
		}
		for (const auto& [id, p] : edits.positions)
			next.layout[id] = {p.first, p.second};
		if (edits.connection || !edits.removeLinks.empty() || !edits.removeNodes.empty() || !edits.positions.empty())
			if (Commit(doc, next, error))
				graph = std::move(next);
	}

	/**
	 * @brief 编辑属性。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] graph 包含当前选中节点及其可编辑参数的材质图。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 */
	void MaterialGraphEditor::EditProperties(
	    AssetDocument& doc, Tools::MaterialGraphSource& graph, const PanelText& text, std::string& error) {
		if (const auto selected =
		        std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.id == m_Selected; });
		    selected != graph.nodes.end()) {
			ImGui::Text("%s: %s", text("graph.selected", "Selected", nullptr).c_str(), selected->id.ToString().c_str());
			if (selected->type == "Constant") {
				if (ImGui::InputScalarN(text("graph.constant", "Constant", "GraphConstant").c_str(),
				        ImGuiDataType_Float, selected->value.data(), int(selected->width), nullptr, nullptr, "%.6g",
				        ImGuiInputTextFlags_EnterReturnsTrue))
					Commit(doc, graph, error);
			} else if (selected->type == "Parameter") {
				const auto preview =
				    text(("graph.parameter." + selected->parameter).c_str(), selected->parameter.c_str(), nullptr);
				if (ImGui::BeginCombo(
				        text("graph.parameter", "Parameter", "GraphParameter").c_str(), preview.c_str())) {
					for (const auto& [name, value] : graph.parameters) {
						if (value.size() != selected->width)
							continue;
						const auto item = text(("graph.parameter." + name).c_str(), name.c_str(), name.c_str());
						if (ImGui::Selectable(item.c_str(), name == selected->parameter)) {
							selected->parameter = name;
							Commit(doc, graph, error);
						}
					}
					ImGui::EndCombo();
				}
			}
		}
		if (ImGui::CollapsingHeader(text("graph.values", "Runtime parameter values", "GraphValues").c_str())) {
			ImGui::TextDisabled("%s",
			    text("graph.values_hint", "Enter commits a value; these parameters reuse the compiled structure.",
			        nullptr)
			        .c_str());
			for (auto& [name, value] : graph.parameters) {
				const auto label = text(("graph.parameter." + name).c_str(), name.c_str(), name.c_str());
				if (FloatFields(label.c_str(), value))
					Commit(doc, graph, error);
			}
		}
	}

	void MaterialGraphEditor::EditGraph(
	    AssetDocument& doc, Tools::MaterialGraphSource& graph, const PanelText& text, std::string& error) {
		EditToolbar(doc, graph, text, error);
		EditCanvas(doc, graph, error);
		EditProperties(doc, graph, text, error);
	}

	/**
	 * @brief 发布预览。
	 *
	 * @param[in] app 提供场景、资产和渲染服务的应用对象。
	 * @param[in] owner 标识持有该对象的上下文。
	 * @param[in] graph 提供预览使用的材质参数和纹理依赖的材质图。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool MaterialGraphEditor::PublishPreview(
	    Application& app, AssetDocument& owner, const Tools::MaterialGraphSource& graph, std::string& error) {
		if (!m_GoodShader || m_GoodStructure != Tools::MaterialGraphStructure(graph))
			return false;
		std::string source, definitionText;
		auto runtimeGraph = graph;
		runtimeGraph.layout.clear();
		/* 画布位置只进入作者文档；移动节点不发布材质版本，也不触发 GPU 数据更新。 */
		if (!Tools::WriteMaterialGraph(runtimeGraph, source, error))
			return false;
		if (source == m_PreviewSource)
			return true;
		if (m_Revision == UINT64_MAX) {
			error = "Graph preview revision exhausted";
			return false;
		}

		std::shared_ptr<const MaterialDefinition> parsed;
		if (!Tools::MaterialGraphCompiler::Definition(graph, m_ShaderId, definitionText, error) ||
		    !ParseMaterialDefinition(definitionText, parsed, error))
			return false;
		auto definition = std::make_shared<MaterialDefinition>(*parsed);
		std::array<AssetVersionRef<ImageAsset>, 5> textures;
		for (const auto& parameter : definition->parameters) {
			const auto* texture = std::get_if<AssetHandle<ImageAsset>>(&parameter.defaultValue);
			if (!texture || !texture->HasValue())
				continue;
			const auto slot = parameter.textureSemantic == MaterialTextureSemantic::BaseColor ? 0
			    : parameter.textureSemantic == MaterialTextureSemantic::Normal                ? 1
			    : parameter.textureSemantic == MaterialTextureSemantic::MetallicRoughness     ? 2
			    : parameter.textureSemantic == MaterialTextureSemantic::Emissive              ? 3
			                                                                                  : 4;
			bool fallback = false;
			if (!app.GetAssets().Acquire(texture->id, textures[slot], fallback, error) || fallback) {
				if (app.GetAssets().GetStatus(texture->id) == AssetStatus::Unloaded) {
					std::string ignored;
					(void)app.GetAssets().Request(texture->id, ignored);
				}
				return false;
			}
			if (!ValidateMaterialTexture(parameter, *textures[slot].Get(), error))
				return false;
		}

		/* 从同一 Manager 重新取得带预算所有权的固定引用；直接包装原 shared_ptr 会绕过退休计账。 */
		const auto revision = ++m_Revision;
		bool fallback = false;
		if (m_PublishedShader.Get() != m_GoodShader.get()) {
			if (!app.GetAssets().PublishBuiltin<ShaderAsset>(
			        m_ShaderId, revision, m_GoodShader, ShaderLibrary::EstimateResidentBytes(*m_GoodShader), error) ||
			    !app.GetAssets().Acquire(m_ShaderId, m_PublishedShader, fallback, error) || fallback)
				return false;
		}
		definition->shaderVersion = m_PublishedShader;
		AssetVersionRef<MaterialDefinition> fixed;
		if (!app.GetAssets().PublishBuiltin<MaterialDefinition>(
		        m_DefinitionId, revision, definition, MaterialDefinitionBytes(*definition), error) ||
		    !app.GetAssets().Acquire(m_DefinitionId, fixed, fallback, error) || fallback)
			return false;

		std::shared_ptr<const MaterialInstance> instance;
		if (!CreateMaterialInstance(fixed, {}, {}, revision, instance, error))
			return false;
		auto bound = std::make_shared<MaterialInstance>(*instance);
		bound->textureVersions = std::move(textures);
		if (!app.GetAssets().PublishBuiltin<MaterialInstance>(
		        m_MaterialId, revision, bound, MaterialInstanceBytes(*bound), error))
			return false;
		AssetVersionRef<MaterialInstance> version;
		if (!app.GetAssets().Acquire(m_MaterialId, version, fallback, error) || fallback)
			return false;
		m_PreviewDocument = std::make_unique<AssetDocument>(owner.Id(), version);
		m_PreviewSource = std::move(source);
		return true;
	}

	/**
	 * @brief 更新编译。
	 *
	 * @param[in] app 提供场景、资产和渲染服务的应用对象。
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] graph 用于核对当前编译请求与源版本、生成着色器的材质图。
	 * @param[out] error 失败时接收原因说明。
	 */
	void MaterialGraphEditor::UpdateCompile(
	    Application& app, AssetDocument& doc, const Tools::MaterialGraphSource& graph, std::string& error) {
		const auto structure = Tools::MaterialGraphStructure(graph);
		if (structure != m_Requested) {
			m_Requested = structure;
			++m_Generation;
			m_Job.Cancel();
		}

		/* 收束旧任务后再检查代次；旧图的完成通知不能覆盖刚提交的新图。 */
		if (m_Task && app.GetJobs().IsComplete(m_Job)) {
			std::string jobError;
			(void)app.GetJobs().Wait(m_Job, jobError);
			{
				std::lock_guard lock(m_Task->mutex);
				if (m_Task->generation == m_Generation) {
					if (m_Task->result.shader) {
						m_GoodShader = m_Task->result.shader;
						m_GoodStructure = m_Task->result.structure;
						m_Diagnostic = m_Task->result.diagnostics;
						m_PreviewSource.clear();
					} else {
						m_Failed = structure;
						m_Diagnostic = m_Task->error.empty() ? jobError : m_Task->error;
						error = m_Diagnostic;
					}
				}
			}
			m_Task.reset();
			m_Job = {};
		}

		if (!m_Task && m_GoodStructure != structure && m_Failed != structure) {
			std::filesystem::path executable;
			const auto* sdk = _wgetenv(L"VULKAN_SDK");
			if (!sdk || !GetExecutableDirectory(executable, error)) {
				if (error.empty())
					error = "Graph compilation requires VULKAN_SDK";
				m_Failed = structure;
				return;
			}
			/* 独占编译任务仅捕获作者值，没有 Application/文档/World 的跨线程借用。 */
			auto task = std::make_shared<Task>();
			task->generation = m_Generation;
			Tools::MaterialGraphCompileRequest request{graph, executable / "Resources/ShaderSources", sdk,
			    std::filesystem::temp_directory_path() / "CatalpaMaterialGraphCache", m_ShaderId, {}};
			const auto work = [task, request = std::move(request)](const CancellationToken& token) mutable {
				request.cancellation = token;
				Tools::MaterialGraphCompileResult result;
				std::string diagnostic;
				const bool ok = Tools::MaterialGraphCompiler::Compile(request, result, diagnostic);
				std::lock_guard lock(task->mutex);
				if (ok)
					task->result = std::move(result);
				task->error = std::move(diagnostic);
			};
			if (app.GetJobs().Submit(work, m_Job, error))
				m_Task = std::move(task);
			else
				m_Failed = structure;
		}
		if (m_GoodStructure == structure)
			(void)PublishPreview(app, doc, graph, error);
	}

	void MaterialGraphEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) {
		if (m_Document != doc.Id()) {
			Close(app);
			const auto shader = RandomAsset(error), definition = RandomAsset(error), material = RandomAsset(error);
			if (!shader.HasValue() || !definition.HasValue() || !material.HasValue())
				return;
			/* 三个身份全部创建后才绑定文档；失败帧允许重试，不留下半初始化编辑会话。 */
			m_ShaderId = shader;
			m_DefinitionId = definition;
			m_MaterialId = material;
			m_Document = doc.Id();
		}
		Tools::MaterialGraphSource graph;
		if (!Tools::ReadMaterialGraph(*doc.Source(), graph, error))
			return;
		EditGraph(doc, graph, text, error);
		std::string validation;
		if (!Tools::ReadMaterialGraph(*doc.Source(), graph, validation)) {
			error = std::move(validation);
			return;
		}
		if (ImGui::Button(text("graph.compile", "Compile / retry", "GraphCompile").c_str()))
			m_Failed.clear();
		if (m_Task) {
			ImGui::SameLine();
			ImGui::TextUnformatted(text("graph.compiling", "Compiling candidate...", nullptr).c_str());
		}
		UpdateCompile(app, doc, graph, error);
		if (!m_Diagnostic.empty())
			ImGui::TextWrapped("%s", m_Diagnostic.c_str());
		if (!m_Diagnostic.empty() &&
		    ImGui::Button(text("graph.clear_diagnostic", "Clear graph diagnostic", "GraphClearDiagnostic").c_str()))
			m_Diagnostic.clear();
		if (m_PreviewDocument) {
			if (m_GoodStructure != Tools::MaterialGraphStructure(graph))
				ImGui::TextDisabled(
				    "%s", text("graph.previous_preview", "Showing the previous valid material.", nullptr).c_str());
			m_Preview.DrawPreview(app, *m_PreviewDocument, error);
		}
	}

	void MaterialGraphEditor::Close(Application& app) {
		++m_Generation;
		m_Job.Cancel();
		if (m_Job.HasValue()) {
			std::string ignored;
			(void)app.GetJobs().Wait(m_Job, ignored);
		}
		m_Task.reset();
		m_Job = {};
		m_Preview.Close(app);
		m_PreviewDocument.reset();
		m_Canvas.Reset();

		/* GPU 帧和缓存继续保活旧版本，此处只撤下本次编辑会话发布的当前版本。 */
		app.GetAssets().Unload(m_MaterialId);
		app.GetAssets().Unload(m_DefinitionId);
		app.GetAssets().Unload(m_ShaderId);
		m_MaterialId = {};
		m_DefinitionId = {};
		m_ShaderId = {};
		m_Document = {};
		m_Selected = {};
		m_Requested.clear();
		m_GoodStructure.clear();
		m_Failed.clear();
		m_PreviewSource.clear();
		m_Diagnostic.clear();
		m_GoodShader.reset();
		m_PublishedShader = {};
		m_Revision = 0;
	}
}
