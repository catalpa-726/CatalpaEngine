#pragma once
#include "Assets/AssetManifest.h"

namespace Catalpa {
	/* 包内仍保存既有不可变 catasset；不把压缩库的内存布局当作资产协议。 */
	struct PackageLimits {
		size_t maxPackageBytes = size_t{2} * 1024 * 1024 * 1024;
		size_t maxAssetBytes = 640 * 1024 * 1024;
		uint32_t maxEntries = 65536;
	};
	struct PackageEntry {
		std::string path;
		size_t offset = 0, bytes = 0;
	};
} /* 命名空间 Catalpa */
