#pragma once
#include "Core/Id/StableIdGenerator.h"
#include "Assets/AssetId.h"

namespace Catalpa::Tools {
    struct MaterialGraphNode {
        StableId id;
        std::string type, parameter, semantic = "baseColor";
        uint32_t width = 1;
        std::array<float, 4> value{0, 0, 0, 1};
        AssetId texture;
        // 端口身份归作者数据所有；名称由节点 schema 定义，画布不生成语义身份。
        std::map<std::string, StableId, std::less<>> ports;
    };
    struct MaterialGraphEndpoint { StableId node, port; };
    struct MaterialGraphLink { StableId id; MaterialGraphEndpoint from, to; };
    struct GraphPosition { float x = 0, y = 0; };
    struct MaterialGraphSource {
        std::string shadingModel = "pbr", alphaMode = "opaque";
        std::vector<MaterialGraphNode> nodes;
        std::vector<MaterialGraphLink> links;
        std::map<StableId, GraphPosition> layout;
        // 固定表面参数通过现有 Material ABI 更新，不作为结构编译键。
        std::map<std::string, std::vector<float>, std::less<>> parameters{
            {"base_color", {1, 1, 1, 1}}, {"emissive", {0, 0, 0}}, {"metallic", {0}},
            {"roughness", {0.65f}}, {"normal_scale", {1}}, {"alpha_cutoff", {0.5f}}};
    };
    struct MaterialGraphPort { std::string name; uint32_t width = 1; bool input = false; };
    std::span<const std::string_view> MaterialNodeTypes();
    std::vector<MaterialGraphPort> MaterialNodePorts(const MaterialGraphNode&);
    bool AddMaterialNode(MaterialGraphSource&, std::string_view type, uint32_t width,
                         StableId& id, std::string& error);
    MaterialGraphSource DefaultMaterialGraph();
    bool ValidateMaterialGraph(const MaterialGraphSource&, std::string& error);
    bool ReadMaterialGraph(std::string_view, MaterialGraphSource&, std::string& error);
    bool WriteMaterialGraph(const MaterialGraphSource&, std::string&, std::string& error);
    std::string MaterialGraphStructure(const MaterialGraphSource&);
}
