#include "MaterialGraph/MaterialGraphSource.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa::Tools {
	namespace {
		using namespace AuthorJson;

		/**
		 * @brief 按稳定 ID 排序编码节点和连线，完整模式另存参数表与画布布局。
		 *
		 * @param[in] graph 提供待编码的节点、端口、连线和编辑器数据。
		 * @param[in] structure 为 true 时仅编码结构签名所需的数据，省略作者参数值与画布布局。
		 *
		 * @return 按稳定标识顺序写入的图 JSON 值；完整模式还包含参数和画布布局。
		 */
		Json Encode(const MaterialGraphSource& graph, bool structure) {
			Json doc{{"schemaVersion", 1}, {"shadingModel", graph.shadingModel}, {"alphaMode", graph.alphaMode},
			    {"nodes", Json::array()}, {"links", Json::array()}};
			if (!structure) {
				doc["parameters"] = graph.parameters;
				doc["layout"] = Json::object();
			}
			std::vector<const MaterialGraphNode*> nodes;
			for (const auto& node : graph.nodes)
				nodes.push_back(&node);
			std::sort(nodes.begin(), nodes.end(), [](auto a, auto b) { return a->id < b->id; });
			for (const auto* n : nodes) {
				Json ports = Json::object();
				for (const auto& [name, id] : n->ports)
					ports[name] = id.ToString();
				doc["nodes"].push_back({{"id", n->id.ToString()}, {"type", n->type}, {"width", n->width},
				    {"parameter", n->parameter}, {"semantic", n->semantic},
				    {"texture", n->texture.HasValue() ? Json(n->texture.ToString()) : Json()}, {"value", n->value},
				    {"ports", ports}});
			}
			std::vector<const MaterialGraphLink*> links;
			for (const auto& link : graph.links)
				links.push_back(&link);
			std::sort(links.begin(), links.end(), [](auto a, auto b) { return a->id < b->id; });
			for (const auto* l : links)
				doc["links"].push_back({{"id", l->id.ToString()},
				    {"from", {{"node", l->from.node.ToString()}, {"port", l->from.port.ToString()}}},
				    {"to", {{"node", l->to.node.ToString()}, {"port", l->to.port.ToString()}}}});
			if (!structure)
				for (const auto& [id, p] : graph.layout)
					doc["layout"][id.ToString()] = Json::array({p.x, p.y});
			return doc;
		}
	}

	bool ValidateMaterialGraph(const MaterialGraphSource& graph, std::string& error) try {
		if (graph.nodes.empty() || graph.nodes.size() > 512 || graph.links.size() > 4096 ||
		    (graph.shadingModel != "pbr" && graph.shadingModel != "unlit") ||
		    (graph.alphaMode != "opaque" && graph.alphaMode != "mask" && graph.alphaMode != "blend"))
			throw std::invalid_argument("Material graph model, output mode or budget is invalid");
		const MaterialGraphSource defaults;
		if (graph.parameters.size() != defaults.parameters.size())
			throw std::invalid_argument("Unknown material parameter table");
		for (const auto& [name, value] : graph.parameters) {
			const auto expected = defaults.parameters.find(name);
			if (expected == defaults.parameters.end() || value.size() != expected->second.size())
				throw std::invalid_argument("Material parameter shape mismatch: " + name);
			for (auto number : value) {
				if (!std::isfinite(number) || std::abs(number) > 1e8f || (name != "normal_scale" && number < 0) ||
				    ((name == "metallic" || name == "roughness") && number > 1))
					throw std::invalid_argument("Material parameter range mismatch: " + name);
			}
			if (name == "base_color" && value[3] > 1)
				throw std::invalid_argument("Base-color alpha must be in [0,1]");
		}
		struct Port {
			StableId node;
			uint32_t width;
			bool input;
		};
		std::map<StableId, const MaterialGraphNode*> nodes;
		std::map<StableId, Port> ports;
		std::set<StableId> allIds;
		std::map<std::string, AssetId, std::less<>> textures;
		size_t outputs = 0;
		for (const auto& node : graph.nodes) {
			if (!node.id.HasValue() || !allIds.insert(node.id).second || node.width < 1 || node.width > 4)
				throw std::invalid_argument("Duplicate node identity or invalid width at " + node.id.ToString());
			nodes.emplace(node.id, &node);
			const auto schema = MaterialNodePorts(node);
			if (schema.empty() || schema.size() != node.ports.size())
				throw std::invalid_argument("Unknown node/port schema at " + node.id.ToString());
			if (node.type == "SurfaceOutput")
				++outputs;
			if (node.type == "Parameter") {
				const auto parameter = graph.parameters.find(node.parameter);
				if (parameter == graph.parameters.end() || parameter->second.size() != node.width)
					throw std::invalid_argument("Parameter node type mismatch at " + node.id.ToString());
			}
			for (auto n : node.value)
				if (!std::isfinite(n))
					throw std::invalid_argument("Non-finite node value");
			if (node.type == "TextureSample") {
				const bool semantic = node.semantic == "baseColor" || node.semantic == "normal" ||
				    node.semantic == "metallicRoughness" || node.semantic == "emissive" || node.semantic == "occlusion";
				if (!semantic || !node.texture.HasValue())
					throw std::invalid_argument(
					    "Texture node needs an asset and supported semantic at " + node.id.ToString());
				if (const auto old = textures.find(node.semantic); old != textures.end() && old->second != node.texture)
					throw std::invalid_argument("This surface ABI permits one texture asset per semantic");
				textures[node.semantic] = node.texture;
			}
			for (const auto& port : schema) {
				const auto found = node.ports.find(port.name);
				if (found == node.ports.end() || !found->second.HasValue() || !allIds.insert(found->second).second)
					throw std::invalid_argument("Missing or duplicate port identity at " + node.id.ToString());
				ports.emplace(found->second, Port{node.id, port.width, port.input});
			}
		}
		if (outputs != 1)
			throw std::invalid_argument("Material graph requires exactly one SurfaceOutput");
		std::set<StableId> connected;
		std::map<StableId, std::vector<StableId>> successors;
		std::map<StableId, size_t> degree;
		for (const auto& [id, n] : nodes)
			degree[id] = 0;
		for (const auto& link : graph.links) {
			const auto from = ports.find(link.from.port), to = ports.find(link.to.port);
			if (!link.id.HasValue() || !allIds.insert(link.id).second || from == ports.end() || to == ports.end() ||
			    from->second.node != link.from.node || to->second.node != link.to.node || from->second.input ||
			    !to->second.input || !connected.insert(link.to.port).second ||
			    (from->second.width != to->second.width && from->second.width != 1))
				throw std::invalid_argument("Invalid connection/type at link " + link.id.ToString());
			successors[link.from.node].push_back(link.to.node);
			++degree[link.to.node];
		}
		std::vector<StableId> ready;
		for (const auto& [id, count] : degree)
			if (!count)
				ready.push_back(id);
		for (size_t i = 0; i < ready.size(); ++i)
			for (auto next : successors[ready[i]])
				if (!--degree[next])
					ready.push_back(next);
		if (ready.size() != nodes.size()) {
			/*
			 * 未排出的节点还包括环的下游；只有回到当前 DFS 路径才证明节点确实位于环上。
			 * 图最多 512 个节点，搜索只在已确定有环的错误路径上执行。
			 */
			enum class Visit { Unseen, Active, Complete };
			std::map<StableId, Visit> visits;
			const auto findCycle = [&](auto&& self, StableId id) -> StableId {
				auto& visit = visits[id];
				if (visit == Visit::Active)
					return id;
				if (visit == Visit::Complete)
					return {};

				visit = Visit::Active;
				for (auto next : successors[id]) {
					if (!degree.at(next))
						continue;
					const auto cycle = self(self, next);
					if (cycle.HasValue())
						return cycle;
				}
				visit = Visit::Complete;
				return {};
			};
			for (const auto& [id, count] : degree) {
				if (!count)
					continue;
				const auto cycle = findCycle(findCycle, id);
				if (cycle.HasValue())
					throw std::invalid_argument("Material DAG cycle at node " + cycle.ToString());
			}
			throw std::invalid_argument("Material DAG contains a cycle");
		}
		for (const auto& [id, p] : graph.layout)
			if (!nodes.contains(id) || !std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1000000 ||
			    std::abs(p.y) > 1000000)
				throw std::invalid_argument("Invalid material layout entry");
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool ReadMaterialGraph(std::string_view text, MaterialGraphSource& output, std::string& error) try {
		const auto doc = Parse(text);
		Fields(doc, {"schemaVersion", "shadingModel", "alphaMode", "parameters", "nodes", "links", "layout"});
		if (Unsigned(doc.at("schemaVersion")) != 1)
			throw std::invalid_argument("Unknown material graph version");
		MaterialGraphSource graph;
		graph.shadingModel = doc.at("shadingModel").get<std::string>();
		graph.alphaMode = doc.at("alphaMode").get<std::string>();
		if (!doc.at("parameters").is_object())
			throw std::invalid_argument("Graph parameters require an object");
		graph.parameters.clear();
		for (const auto& [name, values] : doc.at("parameters").items()) {
			if (!values.is_array() || values.empty() || values.size() > 4)
				throw std::invalid_argument("Invalid parameter width");
			auto& target = graph.parameters[name];
			for (const auto& number : values)
				target.push_back(float(Number(number)));
		}
		if (!doc.at("nodes").is_array() || !doc.at("links").is_array() || !doc.at("layout").is_object())
			throw std::invalid_argument("Graph nodes/links/layout shape mismatch");
		if (doc.at("nodes").size() > 512 || doc.at("links").size() > 4096 || doc.at("layout").size() > 512)
			throw std::invalid_argument("Material graph node/link/layout budget exceeded");
		for (const auto& v : doc.at("nodes")) {
			Fields(v, {"id", "type", "width", "parameter", "semantic", "texture", "value", "ports"});
			MaterialGraphNode n;
			n.id = Identity<StableId>(v.at("id"));
			n.type = v.at("type").get<std::string>();
			const auto width = Unsigned(v.at("width"));
			if (width < 1 || width > 4)
				throw std::invalid_argument("Invalid material width");
			n.width = uint32_t(width);
			n.parameter = v.at("parameter").get<std::string>();
			n.semantic = v.at("semantic").get<std::string>();
			if (!v.at("texture").is_null())
				n.texture = Identity<AssetId>(v.at("texture"));
			if (!v.at("value").is_array() || v.at("value").size() != 4)
				throw std::invalid_argument("Invalid constant shape");
			for (size_t i = 0; i < 4; ++i)
				n.value[i] = float(Number(v.at("value")[i]));
			if (!v.at("ports").is_object())
				throw std::invalid_argument("Invalid port map");
			for (auto it = v.at("ports").begin(); it != v.at("ports").end(); ++it)
				n.ports[it.key()] = Identity<StableId>(it.value());
			graph.nodes.push_back(std::move(n));
		}
		for (const auto& v : doc.at("links")) {
			Fields(v, {"id", "from", "to"});
			Fields(v.at("from"), {"node", "port"});
			Fields(v.at("to"), {"node", "port"});
			graph.links.push_back({Identity<StableId>(v.at("id")),
			    {Identity<StableId>(v.at("from").at("node")), Identity<StableId>(v.at("from").at("port"))},
			    {Identity<StableId>(v.at("to").at("node")), Identity<StableId>(v.at("to").at("port"))}});
		}
		for (auto it = doc.at("layout").begin(); it != doc.at("layout").end(); ++it) {
			if (!it.value().is_array() || it.value().size() != 2)
				throw std::invalid_argument("Invalid node position");
			graph.layout.emplace(Identity<StableId>(Json(it.key())),
			    GraphPosition{float(Number(it.value()[0])), float(Number(it.value()[1]))});
		}
		if (!ValidateMaterialGraph(graph, error))
			return false;
		output = std::move(graph);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool WriteMaterialGraph(const MaterialGraphSource& graph, std::string& out, std::string& error) {
		if (!ValidateMaterialGraph(graph, error))
			return false;
		out = Encode(graph, false).dump(2);
		return true;
	}

	std::string MaterialGraphStructure(const MaterialGraphSource& graph) {
		return Encode(graph, true).dump();
	}
}
