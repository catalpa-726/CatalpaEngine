#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 按规范路径排序已打开的输入，用内容摘要和构建配置生成缓存键。 */
	class AssetDependencyScanner final {
	public:
		/**
		 * @brief 对构建上下文记录的输入依赖按路径排序。
		 *
		 * @param[in] c 参与处理的 AssetBuildContext。
		 *
		 * @return 按路径排序的源依赖记录副本，保留每项的内容哈希与字节数。
		 */
		static std::vector<AssetSourceDependency> Snapshot(const AssetBuildContext&);
		/**
		 * @brief 根据构建签名与排序后的输入依赖计算缓存键。
		 *
		 * @param[in] c 参与处理的 AssetBuildContext。
		 * @param[in] signature 区分工具或导入实现版本的签名，参与派生数据缓存键计算。
		 *
		 * @return 由构建签名、排序的输入记录及纹理重定位映射计算的 SHA-256 缓存键。
		 */
		static std::string Key(const AssetBuildContext&, std::string_view signature);
	};
}
