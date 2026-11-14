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
		/* 端口标识属于作者数据，端口名称由节点模式定义，画布不生成新的语义标识。 */
		std::map<std::string, StableId, std::less<>> ports;
	};
	struct MaterialGraphEndpoint {
		StableId node, port;
	};
	struct MaterialGraphLink {
		StableId id;
		MaterialGraphEndpoint from, to;
	};
	struct GraphPosition {
		float x = 0, y = 0;
	};
	struct MaterialGraphSource {
		std::string shadingModel = "pbr", alphaMode = "opaque";
		std::vector<MaterialGraphNode> nodes;
		std::vector<MaterialGraphLink> links;
		std::map<StableId, GraphPosition> layout;
		/* 固定表面参数通过现有 Material ABI 更新，不作为结构编译键。 */
		std::map<std::string, std::vector<float>, std::less<>> parameters{{"base_color", {1, 1, 1, 1}},
		    {"emissive", {0, 0, 0}}, {"metallic", {0}}, {"roughness", {0.65f}}, {"normal_scale", {1}},
		    {"alpha_cutoff", {0.5f}}};
	};
	struct MaterialGraphPort {
		std::string name;
		uint32_t width = 1;
		bool input = false;
	};
	/**
	 * @brief 返回材质图支持的节点类型描述。
	 *
	 * @return 指向静态内置节点类型名称表的只读视图，可在调用结束后继续使用。
	 */
	std::span<const std::string_view> MaterialNodeTypes();
	/**
	 * @brief 返回材质节点的输入与输出端口。
	 *
	 * @param[in] n 参与处理的 MaterialGraphNode。
	 *
	 * @return 节点的端口描述列表，每项含名称、分量宽度及输入方向；未知节点类型返回空列表。
	 */
	std::vector<MaterialGraphPort> MaterialNodePorts(const MaterialGraphNode&);
	/**
	 * @brief 添加材质节点。
	 *
	 * @param[in,out] graph 接收新节点及其端口的材质源图。
	 * @param[in] type 注册的材质节点类型键。
	 * @param[in] width 节点数值端口的分量数，取值为 1 至 4。
	 * @param[in] id 用于定位目标的稳定标识。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool AddMaterialNode(MaterialGraphSource&, std::string_view type, uint32_t width, StableId& id, std::string& error);
	/**
	 * @brief 返回默认的材质图。
	 *
	 * @return 包含一个 SurfaceOutput 节点的初始材质图；节点创建失败时抛出异常。
	 */
	MaterialGraphSource DefaultMaterialGraph();
	/**
	 * @brief 校验材质图。
	 *
	 * @param[in] graph 要检查节点、端口、连接和环路的材质源图。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ValidateMaterialGraph(const MaterialGraphSource&, std::string& error);
	/**
	 * @brief 读取材质图。
	 *
	 * @param[in] text 待解析并校验的材质图 JSON 文本。
	 * @param[out] output 接收 MaterialGraphSource 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ReadMaterialGraph(std::string_view, MaterialGraphSource&, std::string& error);
	/**
	 * @brief 写入材质图。
	 *
	 * @param[in] graph 要校验并序列化的材质源图。
	 * @param[out] out 接收包含节点、连线和编辑布局的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool WriteMaterialGraph(const MaterialGraphSource&, std::string&, std::string& error);
	std::string MaterialGraphStructure(const MaterialGraphSource&);
}
