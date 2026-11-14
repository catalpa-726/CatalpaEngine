#pragma once
#include "catpch.h"

#include "Assets/AssetId.h"
#include "Assets/AssetType.h"
namespace Catalpa {
	struct AssetDependency {
		AssetId id;
		uint64_t version = 0;
		/* 依赖项完整容器字节的 SHA-256 摘要。 */
		std::string contentHash;
	};
	/*
	 * 格式 1 按小端序保存一个原始载荷块；格式 2 将其封装在有大小上限的 zstd 帧中。
	 * 各字段按协议写入，不直接保存原生结构体的内存布局。头部的身份、类型和依赖信息之后依次为块偏移、长度及载荷的 SHA-256 摘要。
	 */
	struct AssetContainer {
		AssetId id;
		AssetType type;
		uint64_t version = 0;
		uint32_t payloadVersion = 1;
		std::vector<AssetDependency> dependencies;
		std::vector<std::byte> payload;
	};
}
