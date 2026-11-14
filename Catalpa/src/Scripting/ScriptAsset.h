#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"

namespace Catalpa {
	struct ScriptAsset {
		uint32_t schemaVersion = 1;
		std::string source;
		std::map<std::string, AssetId, std::less<>> modules;
		/* 运行产物绑定容器中已核实的模块版本；作者 JSON 只保存上面的逻辑身份。 */
		std::map<std::string, AssetVersionRef<ScriptAsset>, std::less<>> moduleVersions;
	};
	/**
	 * @brief 读取脚本资产。
	 *
	 * @param[in] text 描述脚本来源及其依赖的资产文档文本。
	 * @param[out] output 接收脚本资产结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadScriptAsset(std::string_view, ScriptAsset&, std::string& error);
	/**
	 * @brief 写入脚本资产。
	 *
	 * @param[in] asset 要写入文档的脚本资产及其依赖声明。
	 * @param[out] output 成功时接收记录脚本来源和模块依赖的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteScriptAsset(const ScriptAsset&, std::string&, std::string& error);
	/**
	 * @brief 为脚本资产注册资产加载器。
	 * @details 第 1 个参数（class AssetManager&）为输入。提供资产查找和版本访问的管理器。
	 *
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 操作成功时为 true，失败时为 false。
	 */
	CATALPA_API bool RegisterScriptAssetLoader(class AssetManager&, std::string& error);
}
