#pragma once
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
namespace Catalpa {
	/**
	 * @brief 检查各级网格是否可作为同一基础网格的 LOD。
	 *
	 * @param[in] base 作为材质槽和子网格布局对照的基础网格版本。
	 * @param[in] levels 按细节层级顺序排列的简化网格固定版本。
	 * @param[in] errors 与 levels 一一对应的简化误差，各值须有限、为正且严格递增。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateMeshLodChain(const AssetVersionRef<MeshAsset>& base,
	    std::span<const AssetVersionRef<MeshAsset>> levels, std::span<const float> errors, std::string&);
}
