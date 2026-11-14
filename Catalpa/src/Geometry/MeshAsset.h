#pragma once
#include "catpch.h"

#include "Geometry/MeshData.h"

namespace Catalpa {
	/*
	 * 多实例共用受检CPU几何及稳定材质槽名；不保存实体Transform或GPU所有权。
	 * LOD 链由 ModelAsset/Renderer 关联，蒙皮权重由独立 MeshSkinBinding 持有，不混入基础几何载荷。
	 */
	struct MeshAsset {
		MeshResourceRef mesh;
		/* 子网格索引对应这里的材质槽，缺失的槽应当报告错误。 */
		std::vector<std::string> materialSlots;
		/* 场景文件内嵌的作者几何，后续保存仍内嵌，不能误写成不存在的 cooked locator。 */
		bool authorInline = false;
	};

	/**
	 * @brief 校验网格资产。
	 *
	 * @param[in] asset 待核对几何数据、子网格和材质槽的网格资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] inline bool ValidateMeshAsset(const MeshAsset& asset, std::string& error) {
		const auto status = ValidateMeshResource(asset.mesh);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		if (asset.materialSlots.empty() || asset.materialSlots.size() > 65536) {
			error = "Mesh asset requires 1..65536 material slots";
			return false;
		}
		try {
			std::set<std::string_view> names;
			for (const auto& name : asset.materialSlots) {
				if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
				    !names.insert(name).second) {
					error = "Mesh material slots require unique bounded names";
					return false;
				}
			}
			for (const auto& submesh : asset.mesh->GetData().submeshes) {
				if (submesh.materialSlot >= asset.materialSlots.size()) {
					error = "Submesh material slot is out of range";
					return false;
				}
			}
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}
}
