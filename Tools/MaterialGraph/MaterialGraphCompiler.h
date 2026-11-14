#pragma once
#include "MaterialGraph/MaterialGraphSource.h"
#include "ShaderCompiler/ShaderCompileJob.h"

namespace Catalpa::Tools {
	struct MaterialGraphCompileRequest {
		MaterialGraphSource graph;
		std::filesystem::path templateRoot, sdkRoot, derivedRoot;
		AssetId shader;
		CancellationToken cancellation;
	};
	struct MaterialGraphCompileResult {
		std::shared_ptr<const ShaderAsset> shader;
		std::string structure, diagnostics;
		std::map<std::string, StableId, std::less<>> nodeFiles;
	};
	class MaterialGraphCompiler {
	public:
		/**
		 * @brief 从材质图生成着色器源码，按输入指纹查找缓存或编译新变体。
		 *
		 * @param[in] request 材质图、模板与 SDK 的绝对路径、派生目录和取消令牌。
		 * @param[out] output 成功时接收着色器、图结构文本、节点对应的源文件及诊断信息。
		 * @param[out] error 接收编译或文件处理错误，成功时清空。
		 *
		 * @return 得到与当前图匹配的着色器并提交完整结果时为 true；验证、编译、写入或取消时失败。
		 */
		static bool Compile(const MaterialGraphCompileRequest&, MaterialGraphCompileResult&, std::string& error);
		/**
		 * @brief 结合材质图与表面着色器模板生成 Slang 代码及节点源码映射。
		 *
		 * @param[in] graph 用于生成表面着色器的材质节点、端口与连接。
		 * @param[in] surfaceTemplate 渲染表面模板。
		 * @param[out] slang 接收与表面模板组合生成的 Slang 源码。
		 * @param[in] sourceMap 接收生成源码中虚拟文件名到稳定节点标识的映射，供诊断定位。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Generate(const MaterialGraphSource&, std::string_view surfaceTemplate, std::string& slang,
		    std::map<std::string, StableId, std::less<>>& sourceMap, std::string& error);
		/**
		 * @brief 根据材质图及着色器标识生成参数布局和材质定义。
		 *
		 * @param[in] graph 定义材质参数及其默认值的源图。
		 * @param[in] shader 生成的材质定义要引用的着色器资产标识。
		 * @param[out] json 接收由源图导出的材质定义 JSON。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Definition(const MaterialGraphSource&, AssetId shader, std::string& json, std::string& error);
		/**
		 * @brief 收集从材质输出可达的纹理采样节点所引用的资源标识。
		 *
		 * @param[in] graph 从其材质输出节点追踪纹理依赖的源图。
		 *
		 * @return 从输出可达的纹理采样节点所引用的资产标识，按标识排序并去重。
		 */
		static std::vector<AssetId> Textures(const MaterialGraphSource&);
		/**
		 * @brief 将源模块模板部署到工具可访问的目标目录。
		 * 工具部署复制源模块；Runtime 仍只读取 cooked ShaderAsset。
		 *
		 * @param[in] sourceRoot 模板源模块所在的绝对根目录。
		 * @param[in] destination 要写入工具模板副本的绝对目标根目录。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 着色器模板成功部署时为 true，源或输出路径不满足约束、复制失败时为 false。
		 */
		static bool DeployTemplates(
		    const std::filesystem::path& sourceRoot, const std::filesystem::path& destination, std::string& error);
	};
}
