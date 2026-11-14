#include "catpch.h"

#include "TestRunner.h"
#include "MaterialGraph/MaterialGraphCompiler.h"
#include "Core/PlatformServices.h"
#include "Documents/AssetDocument.h"
#include "Commands/GraphCommands.h"
#include "Core/Serialization/AuthorJson.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa::Tests {
	void RunMaterialGraphTests(TestContext& test) {
		auto graph = Tools::DefaultMaterialGraph();
		std::string error, text;
		test.Check(Tools::WriteMaterialGraph(graph, text, error), "Default graph has one valid SurfaceOutput");
		Tools::MaterialGraphSource roundtrip;
		test.Check(Tools::ReadMaterialGraph(text, roundtrip, error) &&
		        roundtrip.nodes.front().ports == graph.nodes.front().ports,
		    "Graph roundtrip preserves node and port identities");
		const auto structure = Tools::MaterialGraphStructure(graph);
		graph.layout[graph.nodes.front().id] = {70, 80};
		graph.parameters["roughness"] = {.2f};
		test.Check(Tools::MaterialGraphStructure(graph) == structure,
		    "Layout and runtime parameters do not alter the structural compile key");
		StableId color, add;
		test.Check(Tools::AddMaterialNode(graph, "Constant", 4, color, error) &&
		        Tools::AddMaterialNode(graph, "Add", 4, add, error),
		    "Create typed expression nodes");
		const auto node = [&](StableId id) -> Tools::MaterialGraphNode& {
			return *std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.id == id; });
		};
		StableId link;
		GenerateStableId(link, error);
		graph.links.push_back({link, {color, node(color).ports.at("value")}, {add, node(add).ports.at("a")}});
		GenerateStableId(link, error);
		graph.links.push_back({link, {add, node(add).ports.at("value")},
		    {graph.nodes.front().id, graph.nodes.front().ports.at("baseColor")}});
		test.Check(Tools::ValidateMaterialGraph(graph, error), "Valid numeric DAG reaches the surface output");
		{
			/* 固定节点顺序：被阻塞的下游 ID 小于环节点，避免随机 UUID 掩盖错误定位。 */
			const auto identity = [](uint64_t value) { return StableId::FromWords(0x4752415048435943ULL, value); };
			const auto appendNode = [&](Tools::MaterialGraphSource& target, const char* type, uint64_t value) {
				Tools::MaterialGraphNode fixture;
				fixture.id = identity(value);
				fixture.type = type;
				fixture.width = fixture.type == "SurfaceOutput" ? 1 : 4;
				uint64_t port = 0x10000 + value * 16;
				for (const auto& schema : Tools::MaterialNodePorts(fixture))
					fixture.ports.emplace(schema.name, identity(port++));
				target.nodes.push_back(std::move(fixture));
			};
			const auto connect = [&](Tools::MaterialGraphSource& target, uint64_t from, const char* output, uint64_t to,
			                         const char* input, uint64_t linkId) {
				const auto find = [&](uint64_t id) -> const Tools::MaterialGraphNode& {
					return *std::find_if(
					    target.nodes.begin(), target.nodes.end(), [&](const auto& n) { return n.id == identity(id); });
				};
				target.links.push_back({identity(0x100000 + linkId), {identity(from), find(from).ports.at(output)},
				    {identity(to), find(to).ports.at(input)}});
			};

			Tools::MaterialGraphSource cycle;
			appendNode(cycle, "SurfaceOutput", 1);
			appendNode(cycle, "Add", 2);
			appendNode(cycle, "Add", 100);
			appendNode(cycle, "Constant", 200);
			connect(cycle, 200, "value", 100, "a", 1);
			connect(cycle, 100, "value", 100, "b", 2);
			connect(cycle, 100, "value", 2, "a", 3);
			connect(cycle, 2, "value", 1, "baseColor", 4);
			test.Check(
			    !Tools::ValidateMaterialGraph(cycle, error) && error.find(identity(100).ToString()) != error.npos,
			    "Material cycles report a concrete node");

			/* A -> B -> A 的回边位于环内；A -> 下游 -> 输出只被阻塞，不是环。 */
			auto pairedCycle = cycle;
			std::erase_if(pairedCycle.links, [](const auto& edge) { return edge.from.node == edge.to.node; });
			appendNode(pairedCycle, "Add", 101);
			connect(pairedCycle, 100, "value", 101, "a", 5);
			connect(pairedCycle, 101, "value", 100, "b", 6);
			test.Check(!Tools::ValidateMaterialGraph(pairedCycle, error) &&
			        (error.find(identity(100).ToString()) != error.npos ||
			            error.find(identity(101).ToString()) != error.npos) &&
			        error.find(identity(1).ToString()) == error.npos &&
			        error.find(identity(2).ToString()) == error.npos,
			    "A two-node cycle reports an actual cycle member instead of its blocked downstream nodes");

			auto sharedDag = pairedCycle;
			std::erase_if(sharedDag.links,
			    [&](const auto& edge) { return edge.from.node == identity(101) && edge.to.node == identity(100); });
			connect(sharedDag, 101, "value", 2, "b", 7);
			test.Check(Tools::ValidateMaterialGraph(sharedDag, error),
			    "Removing the back edge restores a valid shared DAG without rejecting fan-out and reconvergence");
		}
		auto wrong = graph;
		wrong.links.back().to.port = graph.nodes.front().ports.at("roughness");
		test.Check(!Tools::ValidateMaterialGraph(wrong, error), "Vector-to-scalar connections are rejected");
		const auto shader = AssetId::FromWords(0x4d41544752415048ULL, 1);
		test.Check(Tools::MaterialGraphCompiler::Definition(graph, shader, text, error) &&
		        text.find("catalpa.graph") != text.npos,
		    "Graph compiler emits the ordinary material definition protocol");
		std::filesystem::path executable;
		std::vector<std::byte> bytes;
		const bool available = GetExecutableDirectory(executable, error) &&
		    Tools::ShaderToolDetail::ReadFile(
		        executable / "Resources/ShaderSources/Pbr.slang", 1024 * 1024, bytes, error);
		test.Check(available, "Shader cook deploys the shared source templates used by graph compilation");
		if (available) {
			std::map<std::string, StableId, std::less<>> map;
			std::string generated;
			test.Check(Tools::MaterialGraphCompiler::Generate(
			               graph, {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, generated, map, error) &&
			        generated.find("graphDepthMain") != generated.npos &&
			        generated.find("graphPickingMain") != generated.npos &&
			        generated.find("graphShadowVertex") != generated.npos,
			    "Generated color/depth/shadow/picking entries use one graph evaluation");
			test.Check(map.contains("node_" + color.ToString()) && generated.find("#line") != generated.npos,
			    "Generated expressions retain node diagnostic source mapping");

			/* 验证器允许标量广播；生成器必须在 Alpha 的成员访问之前显式扩展为 float4。 */
			auto channels = Tools::DefaultMaterialGraph();
			StableId scalar, alpha, split, time, image;
			const bool nodesCreated = Tools::AddMaterialNode(channels, "Constant", 1, scalar, error) &&
			    Tools::AddMaterialNode(channels, "Alpha", 1, alpha, error) &&
			    Tools::AddMaterialNode(channels, "Split", 4, split, error) &&
			    Tools::AddMaterialNode(channels, "Time", 1, time, error) &&
			    Tools::AddMaterialNode(channels, "TextureSample", 4, image, error);
			test.Check(nodesCreated, "Create scalar, alpha, packed-channel and time graph fixtures");
			if (nodesCreated) {
				channels.nodes.back().texture = AssetId::FromWords(0x4d41544752415048ULL, 4);
				channels.nodes.back().semantic = "metallicRoughness";
				const auto endpoint = [&](StableId id, std::string_view name) {
					const auto found = std::find_if(
					    channels.nodes.begin(), channels.nodes.end(), [&](const auto& n) { return n.id == id; });
					return Tools::MaterialGraphEndpoint{id, found->ports.at(std::string(name))};
				};
				const auto connect = [&](StableId from, std::string_view output, StableId to, std::string_view input) {
					StableId identity;
					if (!GenerateStableId(identity, error))
						return false;
					channels.links.push_back({identity, endpoint(from, output), endpoint(to, input)});
					return true;
				};
				const auto surface = channels.nodes.front().id;
				test.Check(connect(scalar, "value", alpha, "color") && connect(alpha, "value", surface, "alpha") &&
				        connect(image, "rgba", split, "color") && connect(split, "g", surface, "roughness") &&
				        connect(split, "b", surface, "metallic") && connect(time, "value", surface, "emissive") &&
				        Tools::ValidateMaterialGraph(channels, error),
				    "Packed G/B channels and scalar broadcasts are valid typed connections");

				/* 同一模板在 Windows CRLF 检出后仍应产生完全相同的源代码。 */
				std::string lf(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				std::erase(lf, '\r');
				std::string crlf;
				for (const char c : lf) {
					if (c == '\n')
						crlf += '\r';
					crlf += c;
				}
				std::string generatedLF, generatedCRLF;
				test.Check(Tools::MaterialGraphCompiler::Generate(channels, lf, generatedLF, map, error) &&
				        Tools::MaterialGraphCompiler::Generate(channels, crlf, generatedCRLF, map, error) &&
				        generatedLF == generatedCRLF,
				    "Graph generation is independent of LF or CRLF template checkout");
				auto scalarName = "g_" + scalar.ToString();
				std::erase(scalarName, '-');
				test.Check(generatedLF.find("(float4(" + scalarName + ")).a") != generatedLF.npos,
				    "Scalar-to-Alpha connection generates a vector before the alpha component access");
				test.Check(generatedLF.find("frameData.options.z * 4096.0 + frameData.time.x") != generatedLF.npos,
				    "Time consumes both epoch and phase instead of restarting every 4096 seconds");
				test.Check(map.contains("node_" + surface.ToString() + "_port_" +
				               endpoint(surface, "roughness").port.ToString()),
				    "Surface diagnostics identify both the output node and affected port");
				test.Check(Tools::WriteMaterialGraph(channels, text, error) &&
				        Tools::ReadMaterialGraph(text, roundtrip, error) &&
				        Tools::MaterialGraphStructure(channels) == Tools::MaterialGraphStructure(roundtrip),
				    "Packed-channel node identities survive author save and reopen");
			}
		}
		auto textured = graph;
		StableId texture;
		test.Check(
		    Tools::AddMaterialNode(textured, "TextureSample", 4, texture, error), "Create a graph texture fixture");
		textured.nodes.back().texture = AssetId::FromWords(0x4d41544752415048ULL, 3);
		GenerateStableId(link, error);
		textured.links.push_back({link, {texture, textured.nodes.back().ports.at("r")},
		    {textured.nodes.front().id, textured.nodes.front().ports.at("roughness")}});
		std::shared_ptr<const MaterialDefinition> definition;
		AssetVersionRef<MaterialDefinition> fixed;
		std::shared_ptr<const MaterialInstance> instance;
		test.Check(Tools::MaterialGraphCompiler::Definition(textured, shader, text, error) &&
		        ParseMaterialDefinition(text, definition, error) &&
		        AssetVersionRef<MaterialDefinition>::TryCreate(shader, 1, definition, fixed, error) &&
		        CreateMaterialInstance(fixed, {}, {}, 1, instance, error),
		    "Build a definition with a locked sampler default");
		if (instance) {
			AssetVersionRef<MaterialInstance> source;
			test.Check(AssetVersionRef<MaterialInstance>::TryCreate(shader, 1, instance, source, error),
			    "Fix the material instance fixture");
			Editor::AssetDocument author(StableId::FromWords(77, 1), source);
			test.Check(author.Serialize(text, error) && AuthorJson::Json::parse(text).at("parameters").empty(),
			    "Material author save omits inherited defaults instead of emitting forbidden sampler overrides");
		}
		StableId document;
		GenerateStableId(document, error);
		Editor::AssetDocument doc(document, shader, "material_graph", "before");
		doc.History().MarkSaved();
		test.Check(Editor::ChangeAuthorSource(doc, "after", error) && doc.History().Undo(doc, error) &&
		        *doc.Source() == "before" && !doc.History().Dirty(),
		    "Graph source transactions preserve the saved checkpoint");
		test.Check(Editor::ChangeAuthorSource(doc, "one", error, 99) &&
		        Editor::ChangeAuthorSource(doc, "two", error, 99) && doc.History().Undo(doc, error) &&
		        *doc.Source() == "before",
		    "One text-edit session forms one bounded undo transaction");
	}
}
