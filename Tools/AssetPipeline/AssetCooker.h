#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
	/* 命令行与编辑器共用的同步构建入口。主清单发布前响应取消。 */
	class AssetCooker final {
	public:
		/**
		 * @brief 同步构建源资产，在发布主清单前检查取消请求。
		 *
		 * @param[in] request 项目、源文件、导入设置、取消令牌和进度回调；回调在构建线程上执行。
		 *
		 * @return 构建结果。success 表示是否发布，cacheHit 表示是否复用缓存；失败时 error 和 diagnostics 提供原因。
		 */
		static AssetBuildResult Build(AssetBuildRequest);
	};
}
