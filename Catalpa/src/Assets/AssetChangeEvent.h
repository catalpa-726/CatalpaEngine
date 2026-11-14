#pragma once
#include "Assets/AssetId.h"

namespace Catalpa {
	/* 仅完整新快照发布后产生；旧固定引用继续有效，不强制重绑已有场景。 */
	struct AssetChangeEvent {
		AssetId id;
		uint64_t previousVersion = 0;
		uint64_t version = 0;
		uint64_t sequence = 0;
	};
} /* 命名空间 Catalpa */
