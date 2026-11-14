#pragma once
#include "Geometry/GeometryAssetLoader.h"

namespace Catalpa::Tools {
	/* Mesh编码与Runtime loader共享同一字段协议和受检不可变几何。 */
	class MeshCooker final {
	public:
		static bool Encode(const MeshAsset&, std::vector<std::byte>&, std::string&);
	};
}
