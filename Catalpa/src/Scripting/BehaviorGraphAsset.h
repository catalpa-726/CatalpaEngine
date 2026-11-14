#pragma once
#include "Scripting/ScriptValue.h"
#include "Assets/AssetId.h"

namespace Catalpa {
	struct BehaviorNode {
		StableId id;
		std::string type, variable, event;
		double number = 0;
		Math::Vec3 vector{0};
		std::map<std::string, StableId, std::less<>> flow;
		float x = 0, y = 0;
	};
	struct BehaviorGraphAsset {
		std::vector<BehaviorNode> nodes;
		std::map<std::string, double, std::less<>> variables;
	};
	struct BehaviorNodeDescription {
		std::string_view name;
		std::vector<std::string_view> outputs;
		bool entry = false;
	};
	/**
	 * @brief 返回行为图支持的节点类型描述。
	 *
	 * @return 指向内置行为节点描述的只读视图。
	 */
	CATALPA_API std::span<const BehaviorNodeDescription> BehaviorNodeTypes();
	/**
	 * @brief 读取行为图。
	 *
	 * @param[in] text 描述行为图节点、连接和变量的 JSON 文本。
	 * @param[out] output 接收 BehaviorGraphAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadBehaviorGraph(std::string_view, BehaviorGraphAsset&, std::string& error);
	/**
	 * @brief 写入行为图。
	 *
	 * @param[in] graph 要序列化的行为图资产。
	 * @param[out] output 成功时接收记录行为图节点和执行连接的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteBehaviorGraph(const BehaviorGraphAsset&, std::string&, std::string& error);
	/**
	 * @brief 校验行为图。
	 *
	 * @param[in] graph 要检查节点、边及变量引用的行为图资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateBehaviorGraph(const BehaviorGraphAsset&, std::string& error);
	/**
	 * @brief 校验行为变量。
	 *
	 * @param[in] values 要检查名称及值类型的行为图变量集合。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateBehaviorVariables(const ScriptValues&, std::string& error);
	/**
	 * @brief 为行为图资产注册资产加载器。
	 * @details 第 1 个参数（class AssetManager&）为输入。提供资产查找和版本访问的管理器。
	 *
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 操作成功时为 true，失败时为 false。
	 */
	CATALPA_API bool RegisterBehaviorGraphAssetLoader(class AssetManager&, std::string& error);
}
