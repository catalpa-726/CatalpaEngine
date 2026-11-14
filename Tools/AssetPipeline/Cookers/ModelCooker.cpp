#include "catpch.h"

#include "AssetPipeline/Cookers/ModelCooker.h"

namespace Catalpa::Tools {
	bool ModelCooker::Encode(const ModelAsset& v, std::vector<std::byte>& b, std::string& e) {
		return GeometryAssetLoader::EncodeModel(v, b, e);
	}
}
