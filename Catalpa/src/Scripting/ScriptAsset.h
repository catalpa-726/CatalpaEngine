#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"

namespace Catalpa {
	enum class ScriptLanguage { Lua, CSharp };
	struct ScriptAsset {
		uint32_t schemaVersion = 1;
		std::string source;
		std::map<std::string, AssetId, std::less<>> modules;
		/* 运行产物绑定容器中已核实的模块版本；作者 JSON 只保存上面的逻辑身份。 */
		std::map<std::string, AssetVersionRef<ScriptAsset>, std::less<>> moduleVersions;
		/* 烹饪时记录相对项目源根的路径，用于运行时错误定位。 */
		std::string sourceLabel;
		ScriptLanguage language = ScriptLanguage::Lua;
		std::vector<std::byte> assembly;
	};
	/** @brief 仅读取程序集元数据，确认其中有唯一可实例化的公开行为类型。 */
	CATALPA_API bool ValidateCSharpAssembly(const ScriptAsset& asset, std::string& error);
	/** @brief 将已编译的 C# 程序集与来源名编码为可随游戏包加载的脚本。 */
	CATALPA_API bool EncodeManagedScript(std::string_view sourceLabel, std::span<const std::byte> assembly,
	    std::vector<std::byte>& output, std::string& error);
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
