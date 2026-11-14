#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Shaders/ShaderAsset.h"
#include "Assets/AssetRecord.h"
namespace Catalpa {
	class AssetManager;
	/* 在同一个 AssetManager 中注册着色器解码器，沿用现有资源所有权。 */
	class CATALPA_API ShaderLibrary final {
	public:
		/**
		 * @brief 为资产注册资产加载器。
		 *
		 * @param[in] manager 提供资产查找和版本访问的管理器。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool RegisterAssetLoader(AssetManager&, std::string& error);
		/**
		 * @brief 编码有效载荷。
		 *
		 * @param[in] a 待保存的着色器字节码和反射元数据。
		 * @param[out] output 接收编码后的着色器载荷。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool EncodePayload(const ShaderAsset&, std::vector<std::byte>& output, std::string& error);
		/**
		 * @brief 解码有效载荷。
		 *
		 * @param[in] bytes 加载的着色器资产载荷。
		 * @param[out] output 接收解码并验证的着色器资产。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodePayload(
		    std::span<const std::byte>, std::shared_ptr<const ShaderAsset>& output, std::string& error);
		static size_t EstimateResidentBytes(const ShaderAsset&) noexcept;
		/**
		 * @brief 从规范的着色器源相对路径生成稳定内置资产标识。
		 * 仅名称决定身份；内容与工具链决定版本。路径使用源根相对的规范 UTF-8 '/' 表示。
		 *
		 * @param[in] relativeSource 用于生成内置着色器资产标识的相对路径。
		 * @param[out] output 接收资产标识结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 路径合法且生成了非零标识时为 true，否则为 false。
		 */
		static bool BuiltinId(std::string_view relativeSource, AssetId& output, std::string& error);
		/**
		 * @brief 从部署清单和着色器容器读取内置着色器目录。
		 * 读取部署容器头建立同一资产注册表，不在 Runtime 编译或反射 Shader。
		 *
		 * @param[in] absoluteRoot 包含已部署着色器容器的根目录。
		 * @param[in] mount 为目录记录生成虚拟路径时使用的挂载名。
		 * @param[out] output 成功时接收从各容器头构建的资产记录列表。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 各记录与部署容器均校验通过时为 true，否则为 false。
		 */
		static bool ReadBuiltinCatalog(const std::filesystem::path& absoluteRoot, std::string_view mount,
		    std::vector<AssetRecord>& output, std::string& error);
	};
}
