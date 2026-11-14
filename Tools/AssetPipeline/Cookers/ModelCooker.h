#pragma once
#include "Geometry/GeometryAssetLoader.h"

namespace Catalpa::Tools {
	/* 使用现有的运行时模型协议编码层级以及网格和材质的标识。世界变换及预览覆盖值不参与烘焙。 */
	class ModelCooker final {
	public:
		static bool Encode(const ModelAsset&, std::vector<std::byte>&, std::string&);
	};
}
