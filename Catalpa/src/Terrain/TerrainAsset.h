#pragma once
#include "catpch.h"

#include "Assets/AssetContainerFormat.h"
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
#include "Materials/MaterialInstance.h"
#include "Physics/CollisionShapeAsset.h"
#include "Navigation/NavMeshAsset.h"

namespace Catalpa {
	struct TerrainSettings {
		uint32_t width = 65, height = 65, tileQuads = 32, lodCount = 4;
		float spacing = 1, heightScale = 1, heightOffset = 0, skirtDepth = 32;
		bool buildCollision = true, buildNavigation = false;
	};
	struct TerrainSource {
		std::string heightSource, encoding = "r16_unorm_le";
		TerrainSettings settings;
		AssetId material;
	};
	struct TerrainTileRecord {
		uint32_t x = 0, z = 0, lod = 0;
		AssetDependency mesh, collision;
		Aabb bounds;
	};
	struct TerrainAsset {
		TerrainSettings settings;
		std::string heightDigest;
		AssetDependency material, navigation;
		std::vector<TerrainTileRecord> tiles;
		/* 固定VFS与目录版本，不借用AssetManager。tile worker仅从此快照读取。 */
		std::function<bool(AssetId, size_t, std::vector<std::byte>&, std::string&)> readDeferred;
	};
	struct TerrainHeightData {
		TerrainSettings settings;
		std::vector<float> heights;
	};
	/**
	 * @brief 校验地形设置。
	 *
	 * @param[in] s 待检查的高度网格、分块和细节层级设置。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateTerrainSettings(const TerrainSettings&, std::string&);
	/**
	 * @brief 校验地形高度。
	 *
	 * @param[in] value 含采样设置和对应高度数组的待验证数据。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateTerrainHeights(const TerrainHeightData&, std::string&);
	/**
	 * @brief 读取地形源数据。
	 *
	 * @param[in] text 描述高度数据来源和地形构建设置的 JSON 文本。
	 * @param[out] output 接收地形源数据结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadTerrainSource(std::string_view, TerrainSource&, std::string&);
	/**
	 * @brief 写入地形源数据。
	 *
	 * @param[in] v 待写入文档的高度数据来源与构建设置。
	 * @param[out] output 成功时接收记录地形构建设置和高度采样来源的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteTerrainSource(const TerrainSource&, std::string&, std::string&);
	/**
	 * @brief 读取地形资产。
	 *
	 * @param[in] text 描述地形分块和资源依赖的 JSON 文本。
	 * @param[out] output 接收 TerrainAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadTerrainAsset(std::string_view, TerrainAsset&, std::string&);
	/**
	 * @brief 写入地形资产。
	 *
	 * @param[in] v 要写入文档的地形分块与依赖记录。
	 * @param[out] output 成功时接收记录地形分块及其固定依赖的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteTerrainAsset(const TerrainAsset&, std::string&, std::string&);
	/**
	 * @brief 构建地形瓦片。
	 *
	 * @param[in] data 原始高度采样与地形尺寸设置。
	 * @param[in] x 目标瓦片沿 X 轴的索引。
	 * @param[in] z 目标瓦片沿 Z 轴的索引。
	 * @param[in] lod 细节层级，采样步长为 2 的该次方。
	 * @param[out] output 接收 MeshAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BuildTerrainTile(
	    const TerrainHeightData&, uint32_t x, uint32_t z, uint32_t lod, MeshAsset&, std::string&);
	/**
	 * @brief 构建地形碰撞。
	 *
	 * @param[in] data 原始地形高度数据与设置。
	 * @param[in] x 瓦片 X。
	 * @param[in] z 瓦片 Z。
	 * @param[out] output 接收 CollisionShapeAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BuildTerrainCollision(
	    const TerrainHeightData&, uint32_t x, uint32_t z, CollisionShapeAsset&, std::string&);
	class AssetManager;
	/**
	 * @brief 为地形资产注册资产加载器。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterTerrainAssetLoader(AssetManager&, std::string&);
} /* 命名空间 Catalpa */
