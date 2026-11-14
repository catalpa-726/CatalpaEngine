#pragma once
#include "Navigation/NavMeshAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
	class RecastNavMeshBaker final {
	public:
		/**
		 * @brief 用 Recast 将米制、Y 轴向上的源几何烘焙成单个导航瓦片。
		 * 首版单tile、Y-up米制；不简化或覆盖源几何，只产生独立导航数据。
		 *
		 * @param[in] input 经校验的源顶点、三角形索引和导航构建设置。
		 * @param[in] token 供轮询调用方取消请求的令牌。
		 * @param[out] output 成功时接收烘焙后的导航网格。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 导航网格成功生成并通过校验时为 true；取消或构建失败时为 false。
		 */
		static bool Bake(const NavigationBuildInput&, const CancellationToken&, NavMeshAsset&, std::string& error);
	};
} /* 命名空间 Catalpa::Tools */
