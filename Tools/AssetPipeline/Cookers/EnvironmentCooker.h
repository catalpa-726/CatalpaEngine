#pragma once

#include "Lighting/EnvironmentAssetLoader.h"

namespace Catalpa::Tools {

	/* 环境资源使用独立的类型化容器，烘焙端与运行时加载器共享格式，运行时直接读取该产物。 */
	class EnvironmentCooker final {
	public:
		static bool Encode(const EnvironmentAsset&, std::vector<std::byte>&, std::string& error);
	};

}
