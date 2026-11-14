#pragma once
#include "Core/Base.h"
#include "Assets/AssetId.h"
#include "Materials/MaterialTypes.h"

namespace Catalpa {
	class ComponentRegistry;
	class AssetManager;
	/**
	 * @brief 将引擎支持的持久组件模式注册到指定注册表。
	 * 在组合根注册持久标识、变换、实体关系和网格渲染器的字段模式，底层 ECS 独立于这些领域头文件。
	 *
	 * @param[in,out] registry 接收内置运行时组件模式的注册表。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 所需组件模式成功注册时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool RegisterRuntimeComponentSchemas(ComponentRegistry& registry, std::string& error);
	/**
	 * @brief 在共享的资产管理器中安装各领域的运行时加载器。
	 * 在组合根装配域内loader；Runtime关闭顺序仍由宿主管理，不另建资产/GPU所有者。
	 *
	 * @param[in,out] manager 注册运行时资产解码器的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 全部加载器注册步骤成功时为 true；遇到失败时返回 false。
	 */
	[[nodiscard]] CATALPA_API bool RegisterRuntimeAssetLoaders(AssetManager& manager, std::string& error);
	/**
	 * @brief 选择内置着色模型对应的材质定义或实例标识。
	 * 映射唯一BuiltinAssetIds常量；无效kind返回无效ID，不创建另一套内置资产编号。
	 *
	 * @param[in] kind 选择哪一种内置材质。
	 * @param[in] definition 为 true 时返回定义标识，为 false 时返回实例标识。
	 *
	 * @return 对应的内置资产标识，种类未知时返回无效标识。
	 */
	[[nodiscard]] CATALPA_API AssetId BuiltinMaterialId(BuiltinMaterialKind kind, bool definition = false) noexcept;
}
