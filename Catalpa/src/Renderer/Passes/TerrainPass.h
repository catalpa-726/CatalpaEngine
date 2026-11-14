#pragma once
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {
	struct RenderTerrainTile {
		RenderInstanceId owner;
		uint64_t tileKey = 0;
		AssetVersionRef<MeshAsset> mesh;
		AssetVersionRef<MaterialInstance> material;
		Math::Mat4 world{1};
	};
	class TerrainPass {
	public:
		/**
		 * @brief 将已验证的地形瓦片作为绘制实例追加到场景帧。
		 *
		 * @param[in] packet 待补充地形实例的帧。
		 * @param[in] tiles 提供网格、材质与位置的地形瓦片集合。
		 * @param[out] error 失败时接收瓦片构建错误。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Append(RenderFramePacket&, std::span<const RenderTerrainTile>, std::string&);
	};
} /* 命名空间 Catalpa */
