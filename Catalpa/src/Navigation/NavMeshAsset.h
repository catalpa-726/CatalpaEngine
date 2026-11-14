#pragma once
#include "Navigation/NavigationTypes.h"

namespace Catalpa {
	/*
	 * 单个不可变 Detour tile，格式钉死到当前32-bit-polyref、小端构建。
	 * SDK 对象只在私有 backend 内创建，资产和公共API均只传自有值。
	 */
	struct NavMeshAsset {
		NavigationBuildSettings settings;
		std::vector<std::byte> tile;
	};
	/**
	 * @brief 校验导航网格资产。
	 *
	 * @param[in] asset 包含待检查构建设置和导航数据的导航网格资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateNavMeshAsset(const NavMeshAsset&, std::string& error);
	/**
	 * @brief 读取导航网格资产。
	 *
	 * @param[in] text 含导航网格资产记录的 JSON 文本。
	 * @param[out] output 接收 NavMeshAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadNavMeshAsset(std::string_view, NavMeshAsset&, std::string& error);
	/**
	 * @brief 写入导航网格资产。
	 *
	 * @param[in] value 待写入文档的导航网格资产及其构建数据。
	 * @param[out] output 成功时接收含构建设置及十六进制导航瓦片数据的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteNavMeshAsset(const NavMeshAsset&, std::string&, std::string& error);
	CATALPA_API bool ReadNavigationSource(std::string_view, NavigationBuildInput&, std::string& error);
	/**
	 * @brief 读取导航源。
	 *
	 * @param[in] text 描述导航构建输入的 JSON 文本。
	 * @param[in] defaults 缺省设置。
	 * @param[out] output 接收 NavigationBuildInput 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadNavigationSource(
	    std::string_view, const NavigationBuildSettings& defaults, NavigationBuildInput&, std::string& error);
	/**
	 * @brief 写入导航源。
	 *
	 * @param[in] value 供序列化的导航构建几何和设置。
	 * @param[out] output 成功时接收包含构建几何和导航设置的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteNavigationSource(const NavigationBuildInput&, std::string&, std::string& error);
} /* 命名空间 Catalpa */
