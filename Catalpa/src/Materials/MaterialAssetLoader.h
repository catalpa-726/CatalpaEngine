#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetLoader.h"
#include "Materials/MaterialInstance.h"
namespace Catalpa {
	class AssetManager;
	/**
	 * @brief 估算材质定义及其字符串、参数等动态存储的字节数。
	 *
	 * @param[in] value 待估算常驻字节数的材质定义，包括参数记录及其名称。
	 *
	 * @return 材质定义及其已计入的动态存储所需的字节数。
	 */
	CATALPA_API size_t MaterialDefinitionBytes(const MaterialDefinition&) noexcept;
	/**
	 * @brief 估算材质实例及其参数覆盖值占用的内存。
	 *
	 * @param[in] value 待估算常驻字节数的材质实例，包含覆盖值的存储。
	 *
	 * @return 材质实例及其参数覆盖值所需的字节数。
	 */
	CATALPA_API size_t MaterialInstanceBytes(const MaterialInstance&) noexcept;
	/**
	 * @brief 注册材质资产加载器。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterMaterialAssetLoaders(AssetManager&, std::string& error);
}
