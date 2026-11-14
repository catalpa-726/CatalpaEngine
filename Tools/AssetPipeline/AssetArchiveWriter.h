#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
	class AssetArchiveWriter final {
	public:
		/**
		 * @brief 按照载荷与直接依赖的固定版本编码资产容器和产物记录。
		 * 容器的内容版本同时取决于载荷和每个直接依赖的固定版本。
		 *
		 * @param[in] id 待写入容器的逻辑资产标识。
		 * @param[in] type 容器登记的资产类型。
		 * @param[in] payload 要封装的资产载荷字节。
		 * @param[in] dependencies 直接依赖的资产记录，编码时固定各自的内容版本。
		 * @param[out] out 接收编码后的资产容器。
		 * @param[out] error 失败时接收封装错误原因。
		 *
		 * @return 构建产物的容器字节和版本记录均生成时为 true。
		 */
		static bool Encode(AssetId, std::string_view type, std::span<const std::byte> payload,
		    std::span<const AssetRecord> dependencies, AssetBuildProduct&, std::string& error);
	};
}
