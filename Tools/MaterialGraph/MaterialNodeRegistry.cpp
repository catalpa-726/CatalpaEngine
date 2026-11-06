#include "MaterialGraph/MaterialGraphSource.h"

namespace Catalpa::Tools {
    std::span<const std::string_view> MaterialNodeTypes() {
        static constexpr std::string_view types[]{"Constant", "Parameter", "UV", "Time", "TextureSample",
            "Add", "Multiply", "Lerp", "OneMinus", "Sine", "Normal", "Alpha", "Split", "SurfaceOutput"};
        return types;
    }

    std::vector<MaterialGraphPort> MaterialNodePorts(const MaterialGraphNode& n) {
        if (n.type == "SurfaceOutput") return {{"baseColor", 4, true}, {"metallic", 1, true},
            {"roughness", 1, true}, {"emissive", 3, true}, {"normal", 3, true}, {"alpha", 1, true},
            {"cutoff", 1, true}, {"occlusion", 1, true}};
        if (n.type == "UV") return {{"value", 2, false}};
        if (n.type == "Time") return {{"value", 1, false}};
        if (n.type == "TextureSample") return {{"uv", 2, true}, {"rgba", 4, false}, {"rgb", 3, false}, {"r", 1, false}, {"a", 1, false}};
        if (n.type == "Normal") return {{"rgb", 3, true}, {"uv", 2, true}, {"value", 3, false}};
        if (n.type == "Alpha") return {{"color", 4, true}, {"value", 1, false}};
        // 分量拆分独立于纹理采样，兼容已有图端口，并允许消费 packed G/B 材质通道。
        if (n.type == "Split") return {{"color", 4, true}, {"r", 1, false}, {"g", 1, false}, {"b", 1, false}, {"a", 1, false}};
        if (n.type == "Add" || n.type == "Multiply") return {{"a", n.width, true}, {"b", n.width, true}, {"value", n.width, false}};
        if (n.type == "Lerp") return {{"a", n.width, true}, {"b", n.width, true}, {"t", 1, true}, {"value", n.width, false}};
        if (n.type == "OneMinus" || n.type == "Sine") return {{"input", n.width, true}, {"value", n.width, false}};
        if (n.type == "Constant" || n.type == "Parameter") return {{"value", n.width, false}};
        return {};
    }

    bool AddMaterialNode(MaterialGraphSource& graph, std::string_view type, uint32_t width,
                         StableId& id, std::string& error) {
        if (graph.nodes.size() >= 512 || width < 1 || width > 4 ||
            std::find(MaterialNodeTypes().begin(), MaterialNodeTypes().end(), type) == MaterialNodeTypes().end()) {
            error = "Unknown material node, width or node budget";
            return false;
        }
        MaterialGraphNode node;
        node.type = type;
        node.width = width;
        if (type == "UV") node.width = 2;
        if (type == "Time" || type == "Alpha" || type == "SurfaceOutput") node.width = 1;
        if (type == "TextureSample" || type == "Split") node.width = 4;
        if (type == "Normal") node.width = 3;
        if (type == "Parameter") { node.parameter = width == 4 ? "base_color" : width == 3 ? "emissive" : "roughness"; node.width = width == 2 ? 1 : width; }
        if (!GenerateStableId(node.id, error)) return false;
        for (const auto& port : MaterialNodePorts(node)) {
            StableId value;
            if (!GenerateStableId(value, error)) return false;
            node.ports.emplace(port.name, value);
        }
        id = node.id;
        graph.layout.emplace(id, GraphPosition{float(graph.nodes.size() % 4) * 230, float(graph.nodes.size() / 4) * 200});
        graph.nodes.push_back(std::move(node));
        error.clear();
        return true;
    }

    MaterialGraphSource DefaultMaterialGraph() {
        MaterialGraphSource result;
        StableId id;
        std::string error;
        if (!AddMaterialNode(result, "SurfaceOutput", 1, id, error)) throw std::runtime_error(error);
        return result;
    }
}
