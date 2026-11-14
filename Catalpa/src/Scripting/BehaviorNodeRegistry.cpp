#include "catpch.h"

#include "Scripting/BehaviorGraphAsset.h"
#include "Core/Serialization/AuthorJson.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
	bool ValidateBehaviorVariables(const ScriptValues& values, std::string& error) {
		if (!ValidateScriptValues(values, error))
			return false;
		for (const auto& [name, value] : values) {
			if (std::holds_alternative<double>(value))
				continue;
			/* 行为 VM 使用 double；超过其连续整数精度的 int64 不得悄悄改值。 */
			const auto* integer = std::get_if<int64_t>(&value);
			if (!integer || *integer < -9007199254740992LL || *integer > 9007199254740992LL) {
				error = "Behavior variable must be numeric and exactly representable: " + name;
				return false;
			}
		}
		return true;
	}

	std::span<const BehaviorNodeDescription> BehaviorNodeTypes() {
		static const std::vector<BehaviorNodeDescription> types{{"Start", {"next"}, true}, {"Update", {"next"}, true},
		    {"Event", {"next"}, true}, {"SetNumber", {"next"}}, {"AddNumber", {"next"}},
		    {"Condition", {"true", "false"}}, {"Translate", {"next"}}, {"Wait", {"next"}}, {"EmitEvent", {"next"}},
		    {"Stop", {}}};
		return types;
	}

	bool ValidateBehaviorGraph(const BehaviorGraphAsset& graph, std::string& error) {
		if (graph.nodes.empty() || graph.nodes.size() > 1024 || graph.variables.size() > 256) {
			error = "Behavior graph node/variable bounds exceeded";
			return false;
		}
		std::map<StableId, const BehaviorNode*> ids;
		std::set<std::string> entries;
		for (const auto& [name, value] : graph.variables)
			if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || !std::isfinite(value) ||
			    std::abs(value) > std::numeric_limits<float>::max()) {
				error = "Invalid behavior variable";
				return false;
			}
		for (const auto& node : graph.nodes) {
			const auto type = std::find_if(BehaviorNodeTypes().begin(), BehaviorNodeTypes().end(),
			    [&](const auto& t) { return t.name == node.type; });
			if (!node.id.HasValue() || !ids.emplace(node.id, &node).second || type == BehaviorNodeTypes().end() ||
			    !std::isfinite(node.number) || std::abs(node.number) > std::numeric_limits<float>::max() ||
			    !std::isfinite(node.x) || !std::isfinite(node.y) || std::abs(node.x) > 1000000 ||
			    std::abs(node.y) > 1000000 || node.variable.size() > 128 || node.event.size() > 128 ||
			    node.variable.find('\0') != std::string::npos || node.event.find('\0') != std::string::npos ||
			    !Math::ValidateTRS(Math::TransformTRS{.translation = node.vector}).IsSuccess()) {
				error = "Invalid behavior node: " + node.id.ToString();
				return false;
			}
			if (type->entry) {
				const auto key = node.type == "Event" ? node.event : node.type;
				if (key.empty() || key.size() > 128 || !entries.insert(key).second ||
				    (node.type == "Event" && (key == "Start" || key == "Update"))) {
					error = "Duplicate or empty behavior entry event";
					return false;
				}
			}
			if ((node.type == "Condition" || node.type == "SetNumber" || node.type == "AddNumber") &&
			    !graph.variables.contains(node.variable)) {
				error = "Unknown behavior variable at node " + node.id.ToString();
				return false;
			}
			if ((node.type == "Wait" && node.number < 0) ||
			    (node.type == "EmitEvent" && (node.event.empty() || node.event.size() > 128))) {
				error = "Invalid wait/event at node " + node.id.ToString();
				return false;
			}
			for (const auto& [port, target] : node.flow)
				if (std::find(type->outputs.begin(), type->outputs.end(), port) == type->outputs.end()) {
					error = "Unknown control port at node " + node.id.ToString();
					return false;
				}
		}
		for (const auto& node : graph.nodes)
			for (const auto& [port, target] : node.flow)
				if (!ids.contains(target) || ids.at(target)->type == "Start" || ids.at(target)->type == "Update" ||
				    ids.at(target)->type == "Event") {
					error = "Invalid control target at node " + node.id.ToString() + "/" + port;
					return false;
				}
		if (entries.empty()) {
			error = "Behavior graph has no event entry";
			return false;
		}
		error.clear();
		return true;
	}

	bool ReadBehaviorGraph(std::string_view text, BehaviorGraphAsset& output, std::string& error) try {
		using namespace AuthorJson;
		const auto doc = Parse(text, 4 * 1024 * 1024);
		Fields(doc, {"schemaVersion", "nodes", "variables"});
		if (Unsigned(doc.at("schemaVersion")) != 1 || !doc.at("nodes").is_array() || !doc.at("variables").is_object())
			throw std::invalid_argument("Invalid behavior graph schema");
		if (doc.at("nodes").size() > 1024 || doc.at("variables").size() > 256)
			throw std::invalid_argument("Behavior graph author budget exceeded");
		BehaviorGraphAsset graph;
		for (auto it = doc.at("variables").begin(); it != doc.at("variables").end(); ++it)
			graph.variables.emplace(it.key(), Number(it.value()));
		for (const auto& v : doc.at("nodes")) {
			Fields(v, {"id", "type", "variable", "event", "number", "vector", "flow", "position"});
			BehaviorNode n;
			n.id = Identity<StableId>(v.at("id"));
			n.type = v.at("type").get<std::string>();
			n.variable = v.at("variable").get<std::string>();
			n.event = v.at("event").get<std::string>();
			n.number = Number(v.at("number"));
			n.vector = Vec3(v.at("vector"));
			if (!v.at("flow").is_object() || !v.at("position").is_array() || v.at("position").size() != 2)
				throw std::invalid_argument("Invalid behavior links/position");
			for (auto it = v.at("flow").begin(); it != v.at("flow").end(); ++it)
				n.flow.emplace(it.key(), Identity<StableId>(it.value()));
			n.x = float(Number(v.at("position")[0]));
			n.y = float(Number(v.at("position")[1]));
			graph.nodes.push_back(std::move(n));
		}
		if (!ValidateBehaviorGraph(graph, error))
			return false;
		output = std::move(graph);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool WriteBehaviorGraph(const BehaviorGraphAsset& graph, std::string& output, std::string& error) {
		if (!ValidateBehaviorGraph(graph, error))
			return false;
		using AuthorJson::Json;
		Json doc{{"schemaVersion", 1}, {"variables", graph.variables}, {"nodes", Json::array()}};
		for (const auto& n : graph.nodes) {
			Json links = Json::object();
			for (const auto& [key, target] : n.flow)
				links[key] = target.ToString();
			doc["nodes"].push_back({{"id", n.id.ToString()}, {"type", n.type}, {"variable", n.variable},
			    {"event", n.event}, {"number", n.number}, {"vector", AuthorJson::Array(n.vector)}, {"flow", links},
			    {"position", Json::array({n.x, n.y})}});
		}
		output = doc.dump(2);
		return true;
	}

	bool RegisterBehaviorGraphAssetLoader(AssetManager& manager, std::string& error) {
		AssetLoader loader;
		if (!AssetType::TryParse("behavior_graph", loader.type))
			return false;
		loader.cppType = typeid(BehaviorGraphAsset);
		loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& output,
		                    std::string& error) {
			if (c.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
			    c.payload.size() > context.maxResidentBytes / 4) {
				error = "Behavior graph payload, budget or cancellation";
				return false;
			}
			auto value = std::make_shared<BehaviorGraphAsset>();
			if (!ReadBehaviorGraph(
			        std::string_view(reinterpret_cast<const char*>(c.payload.data()), c.payload.size()), *value, error))
				return false;
			output = MakeAssetLoadResult<BehaviorGraphAsset>(value, c.payload.size() * 4 + sizeof(BehaviorGraphAsset));
			return true;
		};
		return manager.RegisterLoader(std::move(loader), error);
	}
}
