#include "MaterialGraph/MaterialGraphCompiler.h"

#include "Core/Serialization/AuthorJson.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/PlatformServices.h"
#include "Materials/MaterialValidation.h"
#include "Shaders/ShaderLibrary.h"

#include <charconv>

namespace Catalpa::Tools {
	namespace {
		using namespace AuthorJson;

		std::string Type(uint32_t width) {
			return width == 1 ? "float" : "float" + std::to_string(width);
		}

		std::string NumberText(float value) {
			std::array<char, 64> buffer{};
			const auto result =
			    std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, 9);
			if (result.ec != std::errc{})
				throw std::invalid_argument("Cannot format material number");
			auto text = std::string(buffer.data(), result.ptr);
			if (text.find_first_of(".eE") == text.npos)
				text += ".0";
			return text;
		}

		void Replace(std::string& source, std::string_view from, std::string_view to) {
			const auto index = source.find(from);
			if (index == source.npos)
				throw std::invalid_argument("Surface shader template contract changed: " + std::string(from));
			source.replace(index, from.size(), to);
		}
		struct Expressions {
			const MaterialGraphSource& graph;
			std::map<StableId, std::string> ports;
			std::set<StableId> visited;
			std::string body;
			std::map<std::string, StableId, std::less<>> files;

			const MaterialGraphNode& Node(StableId id) const {
				return *std::find_if(
				    graph.nodes.begin(), graph.nodes.end(), [&](const auto& node) { return node.id == id; });
			}

			/**
			 * @brief 从连线生成指定输入端口的表达式，未连接时采用后备表达式。
			 *
			 * @param[in] node 拥有待求值输入端口的材质图节点。
			 * @param[in] name 输入端口在节点模式中的名称。
			 * @param[in] fallback 该端口未连接时使用的着色器后备表达式。
			 *
			 * @return 连接到该端口的表达式，必要时扩展为目标向量；端口未连接时返回 fallback。
			 */
			std::string Input(const MaterialGraphNode& node, std::string_view name, std::string fallback) {
				const auto port = node.ports.at(std::string(name));
				const auto link = std::find_if(
				    graph.links.begin(), graph.links.end(), [&](const auto& item) { return item.to.port == port; });
				if (link == graph.links.end())
					return fallback;
				const auto& source = Node(link->from.node);
				Emit(source);
				const auto inputs = MaterialNodePorts(node);
				const auto outputs = MaterialNodePorts(source);
				const auto input =
				    std::find_if(inputs.begin(), inputs.end(), [&](const auto& p) { return p.name == name; });
				const auto output = std::find_if(outputs.begin(), outputs.end(),
				    [&](const auto& p) { return source.ports.at(p.name) == link->from.port; });
				auto expression = ports.at(link->from.port);
				/* 验证器允许标量广播；在使用 .a 等成员之前必须先物化目标向量类型。 */
				if (output->width == 1 && input->width > 1)
					expression = Type(input->width) + "(" + expression + ")";
				return expression;
			}

			/**
			 * @brief 为节点及其依赖生成 Slang 表达式，避免重复发射。
			 *
			 * @param[in] n 参与处理的 MaterialGraphNode。
			 */
			void Emit(const MaterialGraphNode& n) {
				if (!visited.insert(n.id).second)
					return;
				auto variable = "g_" + n.id.ToString();
				std::erase(variable, '-');
				std::string expr;
				const auto input = [&](std::string_view name, std::string value = "0.0") {
					return Input(n, name, std::move(value));
				};
				if (n.type == "Constant") {
					expr = Type(n.width) + "(";
					for (uint32_t i = 0; i < n.width; ++i)
						expr += (i ? "," : "") + NumberText(n.value[i]);
					expr += ")";
				} else if (n.type == "Parameter") {
					static const std::map<std::string, std::string, std::less<>> fields{
					    {"base_color", "Material.base_color"}, {"emissive", "Material.emissive_metallic.rgb"},
					    {"metallic", "Material.emissive_metallic.w"},
					    {"roughness", "Material.roughness_normal_occlusion_cutoff.x"},
					    {"normal_scale", "Material.roughness_normal_occlusion_cutoff.y"},
					    {"alpha_cutoff", "Material.roughness_normal_occlusion_cutoff.w"}};
					expr = fields.at(n.parameter);
				} else if (n.type == "UV")
					expr = "uv";
				/* 复用既有 epoch/phase 协议，避免 Time 在每4096秒跳回零。最终精度仍为 GPU float。 */
				else if (n.type == "Time")
					expr = "(frameData.options.z * 4096.0 + frameData.time.x)";
				else if (n.type == "TextureSample")
					expr = n.semantic + "_texture.Sample(" + n.semantic + "Sampler," + input("uv", "uv") + ")";
				else if (n.type == "Add")
					expr = "(" + input("a") + "+" + input("b") + ")";
				else if (n.type == "Multiply")
					expr = "(" + input("a", "1.0") + "*" + input("b", "1.0") + ")";
				else if (n.type == "Lerp")
					expr = "lerp(" + input("a") + "," + input("b", "1.0") + "," + input("t", "0.5") + ")";
				else if (n.type == "OneMinus")
					expr = "(1.0-" + input("input") + ")";
				else if (n.type == "Sine")
					expr = "sin(" + input("input") + ")";
				else if (n.type == "Normal") {
					auto defaultUV = std::string("uv");
					const auto source = std::find_if(graph.links.begin(), graph.links.end(),
					    [&](const auto& l) { return l.to.port == n.ports.at("rgb"); });
					if (source != graph.links.end() && Node(source->from.node).type == "TextureSample")
						defaultUV = Input(Node(source->from.node), "uv", "uv");
					expr = "graphDecodeNormal(input," + input("uv", defaultUV) + "," +
					    input("rgb", "float3(0.5,0.5,1.0)") + ")";
				} else if (n.type == "Split")
					expr = input("color", "float4(0.0)");
				else if (n.type == "Alpha")
					expr = "(" + input("color", "float4(1.0)") + ").a";
				else
					throw std::invalid_argument("Unsupported expression node");
				const auto file = "node_" + n.id.ToString();
				files.emplace(file, n.id);
				const auto width = n.type == "TextureSample" || n.type == "Split" ? 4
				    : n.type == "Normal"                                          ? 3
				    : n.type == "Alpha" || n.type == "Time"                       ? 1
				    : n.type == "UV"                                              ? 2
				                                                                  : n.width;
				body += "#line 1 \"" + file + "\"\n    " + Type(width) + " " + variable + " = " + expr + ";\n";
				for (const auto& port : MaterialNodePorts(n)) {
					if (port.input)
						continue;
					ports[n.ports.at(port.name)] = variable +
					    ((n.type == "TextureSample" && port.name != "rgba") || n.type == "Split" ? "." + port.name
					                                                                             : "");
				}
			}
		};

		std::set<StableId> Used(const MaterialGraphSource& graph) {
			const auto root = std::find_if(
			    graph.nodes.begin(), graph.nodes.end(), [](const auto& n) { return n.type == "SurfaceOutput"; });
			std::set<StableId> used;
			if (root == graph.nodes.end())
				return used;
			std::vector<StableId> todo{root->id};
			while (!todo.empty()) {
				const auto id = todo.back();
				todo.pop_back();
				if (!used.insert(id).second)
					continue;
				for (const auto& link : graph.links)
					if (link.to.node == id)
						todo.push_back(link.from.node);
			}
			return used;
		}

		/**
		 * @brief 复制输入源。
		 *
		 * @param[in] source 输入的文件系统路径。
		 * @param[in] destination 目标位置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool CopySources(
		    const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error) {
			VirtualFileSystem files;
			if (!source.is_absolute() || !destination.is_absolute() ||
			    !files.MountReadOnly("templates", source, 0, error))
				return false;
			size_t total = 0, count = 0;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
				if (entry.is_symlink()) {
					error = "Shader template tree contains a link";
					return false;
				}
				if (!entry.is_regular_file() || entry.path().extension() != L".slang")
					continue;
				const auto relative = entry.path().lexically_relative(source);
				VirtualPath path;
				std::vector<std::byte> bytes;
				if (!VirtualPath::Parse("templates:/" + ShaderToolDetail::PathText(relative), path, error) ||
				    !files.ReadFile(path, 1024 * 1024, bytes, error))
					return false;
				total += bytes.size();
				if (++count > 128 || total > 8 * 1024 * 1024) {
					error = "Shader template budget exceeded";
					return false;
				}
				const auto target = destination / relative;
				std::filesystem::create_directories(target.parent_path());
				if (!IsPathWithinRoot(
				        std::filesystem::canonical(target.parent_path()), std::filesystem::canonical(destination)) ||
				    !AtomicFileWriter::Write(target, bytes, error))
					return false;
			}
			return true;
		}
	}

	bool MaterialGraphCompiler::Generate(const MaterialGraphSource& graph, std::string_view surfaceTemplate,
	    std::string& output, std::map<std::string, StableId, std::less<>>& sourceMap, std::string& error) try {
		if (!ValidateMaterialGraph(graph, error))
			return false;
		const auto& root = *std::find_if(
		    graph.nodes.begin(), graph.nodes.end(), [](const auto& n) { return n.type == "SurfaceOutput"; });
		Expressions e{graph};
		const auto base = e.Input(root, "baseColor", "Material.base_color");
		const auto metal = e.Input(root, "metallic", "Material.emissive_metallic.w");
		const auto rough = e.Input(root, "roughness", "Material.roughness_normal_occlusion_cutoff.x");
		const auto emissive = e.Input(root, "emissive", "Material.emissive_metallic.rgb");
		const auto normal = e.Input(root, "normal", "unitNormal(input.normal)");
		const auto alpha = e.Input(root, "alpha", "result.base.a");
		const auto cutoff = e.Input(root, "cutoff", "Material.roughness_normal_occlusion_cutoff.w");
		const auto occlusion = e.Input(root, "occlusion", "1.0");
		const auto outputLine = [&](std::string_view port, std::string statement) {
			const auto file = "node_" + root.id.ToString() + "_port_" + root.ports.at(std::string(port)).ToString();
			e.files.emplace(file, root.id);
			return "#line 1 \"" + file + "\"\n    " + statement + "\n";
		};
		std::string functions = R"SLANG(
// Normal 节点输出世界法线。按实际采样 UV 的导数构造基，保留图内镜像、旋转和缩放的方向。
float3 graphDecodeNormal(MeshVertexOutput input,float2 uv,float3 encoded)
{
    float3 n=unitNormal(input.normal);
    float3 t=unitNormal(input.tangent.xyz-n*dot(n,input.tangent.xyz));
    float3 b=cross(n,t)*input.tangent.w;
    float3 dx=ddx(input.worldPosition),dy=ddy(input.worldPosition);
    float2 ux=ddx(uv),uy=ddy(uv);
    float determinant=ux.x*uy.y-ux.y*uy.x;
    if(abs(determinant)>1e-12)
    {
        float3 du=(dx*uy.y-dy*ux.y)/determinant;
        float3 dv=(dy*ux.x-dx*uy.x)/determinant;
        t=unitNormal(du-n*dot(n,du));
        b=cross(n,t)*(dot(cross(n,t),dv)<0?-1.0:1.0);
        // 来源图像的上下行转换只改变采样行，不反转作者法线的绿色通道。
        if(surfaceFlag(128u)) b=-b;
    }
    float3 mapped=encoded*2.0-1.0;
    mapped.xy*=Material.roughness_normal_occlusion_cutoff.y;
    return unitNormal(t*mapped.x+b*mapped.y+n*mapped.z);
}
)SLANG";
		functions +=
		    "\nstruct GraphValues { float4 base; float metallic; float roughness; float3 emissive; float3 normal; float cutoff; float occlusion; };\n"
		    "GraphValues evaluateGraph(MeshVertexOutput input)\n{\n    float2 uv=surfaceUV(input.uv); float4 vertexColor=input.vertexColor;\n" +
		    e.body +
		    "#line 1 \"graph_surface\"\n    if (surfaceFlags() > 511u) clip(-1.0);\n    GraphValues result;\n" +
		    outputLine("baseColor", "result.base = float4(" + base + ") * drawData.color * vertexColor;") +
		    outputLine("alpha", "result.base.a = saturate(" + alpha + ");") +
		    outputLine("metallic", "result.metallic = saturate(" + metal + ");") +
		    outputLine("roughness", "result.roughness = saturate(" + rough + ");") +
		    outputLine("emissive", "result.emissive = " + emissive + ";") +
		    outputLine("normal", "result.normal = " + normal + ";") +
		    outputLine("cutoff", "result.cutoff = max(0.0," + cutoff + ");") +
		    outputLine("occlusion", "result.occlusion = saturate(" + occlusion + ");") +
		    "#line 1 \"graph_surface\"\n    return result;\n}\n"
		    "void graphMask(GraphValues value) { if (surfaceFlag(32u)) clip(value.base.a-value.cutoff); }\n"
		    "float3 graphNormal(MeshVertexOutput input, GraphValues value, bool frontFace)\n{\n"
		    "    float3 n=unitNormal(value.normal); return frontFace?n:-n;\n}\n";
		std::string source(surfaceTemplate);
		/* Git/编辑器可能把同一模板检出为 CRLF；语义替换不依赖宿主换行约定。 */
		for (size_t i = 0; (i = source.find("\r\n", i)) != source.npos;)
			source.erase(i, 1);
		if (graph.shadingModel == "pbr") {
			Replace(source, "float4 base = surfaceBase(uv, input.vertexColor);",
			    "GraphValues graph = evaluateGraph(input);\n    float4 base = graph.base;");
			Replace(source, "applySurfaceMask(base.a);", "graphMask(graph);");
			Replace(source, "surfaceNormal(input, uv, frontFace)", "graphNormal(input, graph, frontFace)");
			Replace(source, "float metallic = Material.emissive_metallic.w;", "float metallic = graph.metallic;");
			Replace(source, "float roughness = Material.roughness_normal_occlusion_cutoff.x;",
			    "float roughness = graph.roughness;");
			Replace(source, "if (surfaceFlag(4u))", "if (false)");
			Replace(source, "float3 emissive = Material.emissive_metallic.rgb;", "float3 emissive = graph.emissive;");
			Replace(source, "if (surfaceFlag(8u))", "if (false)");
			Replace(source,
			    "float occlusion = surfaceFlag(16u) ? lerp(1.0, occlusion_texture.Sample(occlusionSampler, uv).r,\n"
			    "        Material.roughness_normal_occlusion_cutoff.z) : 1.0;",
			    "float occlusion = graph.occlusion;");
		} else {
			Replace(source, "float4 color = surfaceBase(surfaceUV(input.uv), input.vertexColor);",
			    "GraphValues graph = evaluateGraph(input);\n    float4 color = graph.base; color.rgb += graph.emissive;");
			Replace(source, "applySurfaceMask(color.a);", "graphMask(graph);");
		}
		const auto firstEntry = source.find("[shader(");
		if (firstEntry == source.npos)
			throw std::invalid_argument("Missing surface shader entries");
		source.insert(firstEntry, functions);
		source += R"SLANG(
[shader("fragment")]
void graphDepthMain(MeshVertexOutput input) { graphMask(evaluateGraph(input)); }
[shader("vertex")]
MeshVertexOutput graphShadowVertex(MeshVertexInput input)
{
    MeshVertexOutput result=transformMeshVertex(input);
    result.position=multiplyColumns(lightingData.shadowViewProjection[0],lightingData.shadowViewProjection[1],
        lightingData.shadowViewProjection[2],lightingData.shadowViewProjection[3],float4(result.worldPosition,1));
    return result;
}
[shader("fragment")]
uint graphPickingMain(MeshVertexOutput input) : SV_Target0
{
    GraphValues value=evaluateGraph(input);
    graphMask(value); if(surfaceFlag(64u)) clip(value.base.a-frameData.options.y);
    return drawData.ids.x;
}
)SLANG";
		sourceMap = std::move(e.files);
		output = std::move(source);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	std::vector<AssetId> MaterialGraphCompiler::Textures(const MaterialGraphSource& graph) {
		const auto used = Used(graph);
		std::set<AssetId> ids;
		for (const auto& n : graph.nodes)
			if (used.contains(n.id) && n.type == "TextureSample")
				ids.insert(n.texture);
		return {ids.begin(), ids.end()};
	}

	bool MaterialGraphCompiler::Definition(
	    const MaterialGraphSource& graph, AssetId shader, std::string& output, std::string& error) try {
		if (!ValidateMaterialGraph(graph, error))
			return false;
		if (!shader.HasValue()) {
			error = "Material graph definition requires a shader asset identity";
			return false;
		}
		Json parameters = Json::array();
		for (const auto& [name, values] : graph.parameters) {
			Json value = values.size() == 1 ? Json(values.front()) : Json(values);
			const auto type = values.size() == 1 ? "float" : "float" + std::to_string(values.size());
			Json field{{"id", name}, {"type", type}, {"default", value}};
			if (name != "normal_scale")
				field["min"] = 0;
			if (name == "roughness" || name == "metallic")
				field["max"] = 1;
			parameters.push_back(std::move(field));
		}
		parameters.push_back({{"id", "uv_scale_offset"}, {"type", "float4"}, {"default", Json::array({1, 1, 0, 0})}});
		parameters.push_back({{"id", "uv_cycles"}, {"type", "float2"}, {"default", Json::array({0, 0})}});
		parameters.push_back({{"id", "pulse_cycles"}, {"type", "uint"}, {"default", 0}});
		parameters.push_back({{"id", "occlusion_strength"}, {"type", "float"}, {"default", 1}});
		const auto used = Used(graph);
		std::set<std::string> semantics;
		for (const auto& n : graph.nodes)
			if (used.contains(n.id) && n.type == "TextureSample" && semantics.insert(n.semantic).second) {
				parameters.push_back(
				    {{"id", n.semantic + "_texture"}, {"type", "texture2d"}, {"default", n.texture.ToString()},
				        {"textureSemantic", n.semantic}, {"allowTextureFallback", false}});
				parameters.push_back({{"id", n.semantic + "_sampler"}, {"type", "uint"}, {"default", 240}, {"min", 0},
				    {"max", 255}, {"allowOverride", false}});
			}
		const Json doc{{"schemaVersion", 1}, {"kind", "definition"}, {"family", "catalpa.graph"},
		    {"shadingModel", graph.shadingModel}, {"domain", "surface"}, {"alphaMode", graph.alphaMode},
		    {"shader", shader.ToString()}, {"vertexEntry", "vertexMain"}, {"fragmentEntry", "fragmentMain"},
		    {"parameterBlock", "Material"}, {"parameters", parameters},
		    {"renderState",
		        {{"cull", "none"}, {"depthTest", true}, {"depthWrite", graph.alphaMode != "blend"},
		            {"blend", graph.alphaMode == "blend" ? "alpha" : "opaque"}}},
		    {"allowedStateOverrides", Json::array()}};
		auto text = doc.dump();
		std::shared_ptr<const MaterialDefinition> checked;
		if (!ParseMaterialDefinition(text, checked, error))
			return false;
		output = std::move(text);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool MaterialGraphCompiler::DeployTemplates(
	    const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error) try {
		if (!source.is_absolute() || !destination.is_absolute()) {
			error = "Template deployment requires absolute roots";
			return false;
		}
		const auto input = std::filesystem::canonical(source), target = std::filesystem::weakly_canonical(destination);
		if (IsPathWithinRoot(input, target) || IsPathWithinRoot(target, input)) {
			error = "Template deployment overlaps its source";
			return false;
		}
		std::filesystem::create_directories(target);
		return CopySources(input, target, error);
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool MaterialGraphCompiler::Compile(
	    const MaterialGraphCompileRequest& request, MaterialGraphCompileResult& output, std::string& error) try {
		if (!ValidateMaterialGraph(request.graph, error))
			return false;
		if (!request.derivedRoot.is_absolute() || !request.templateRoot.is_absolute() ||
		    !request.sdkRoot.is_absolute()) {
			error = "Material graph compilation requires absolute template, SDK and derived paths";
			return false;
		}
		const auto root = std::filesystem::weakly_canonical(request.derivedRoot);
		const auto source = std::filesystem::canonical(request.templateRoot),
		           sdk = std::filesystem::canonical(request.sdkRoot);
		if (IsPathWithinRoot(root, source) || IsPathWithinRoot(source, root) || IsPathWithinRoot(root, sdk) ||
		    IsPathWithinRoot(sdk, root)) {
			error = "Material graph derived root overlaps read-only input";
			return false;
		}
		std::filesystem::create_directories(root);
		StableId id;
		if (!GenerateStableId(id, error))
			return false;
		const auto workspace = root / std::filesystem::u8path("graph-" + id.ToString());
		if (!std::filesystem::create_directory(workspace)) {
			error = "Cannot create exclusive graph workspace";
			return false;
		}
		struct Temporary {
			std::filesystem::path path, root;

			~Temporary() {
				std::error_code ec;
				if (IsPathWithinRoot(path, root) && path != root && !std::filesystem::is_symlink(path, ec))
					std::filesystem::remove_all(path, ec);
			}
		} temporary{workspace, root};
		if (!CopySources(source, workspace, error))
			return false;
		if (request.cancellation.IsCancellationRequested()) {
			error = "Material graph compilation cancelled";
			return false;
		}
		std::vector<std::byte> bytes;
		const auto file = workspace / (request.graph.shadingModel == "pbr" ? L"Pbr.slang" : L"Unlit.slang");
		if (!ShaderToolDetail::ReadFile(file, 1024 * 1024, bytes, error))
			return false;
		MaterialGraphCompileResult result;
		std::string generated;
		if (!Generate(request.graph, {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, generated,
		        result.nodeFiles, error))
			return false;
		const auto graphFile = workspace / L"Graph.slang";
		if (!AtomicFileWriter::Write(graphFile, generated, error))
			return false;
		ShaderCompileRequest job{workspace, graphFile, sdk, {}, request.cancellation};
		std::string fingerprint;
		if (!ShaderCompileJob::Fingerprint(job, fingerprint, error))
			return false;
		const auto cache = root / std::filesystem::u8path(fingerprint + ".shader");
		std::string ignored;
		if (ShaderToolDetail::ReadFile(cache, 32 * 1024 * 1024, bytes, ignored) &&
		    ShaderLibrary::DecodePayload(bytes, result.shader, ignored) &&
		    result.shader->variant.inputFingerprint == fingerprint) {
			result.diagnostics = "Material graph shader cache hit";
		} else {
			ShaderCompileResult compiled;
			if (!ShaderCompileJob::Run(job, compiled, error))
				return false;
			result.shader = std::move(compiled.asset);
			result.diagnostics = std::move(compiled.diagnostics);
			if (!ShaderLibrary::EncodePayload(*result.shader, bytes, error) ||
			    request.cancellation.IsCancellationRequested() || !AtomicFileWriter::Write(cache, bytes, error))
				return false;
		}
		if (request.cancellation.IsCancellationRequested()) {
			error = "Material graph compilation cancelled";
			return false;
		}
		result.structure = MaterialGraphStructure(request.graph);
		output = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
