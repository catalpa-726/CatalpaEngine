#pragma once
#include "catpch.h"

#include "Assets/AssetLoader.h"
namespace Catalpa {
	/*
	 * 领域编解码器共用运行时解码器，工具端还可校验并编码载荷。
	 * 稳定类型键和 payloadVersion 共同选择数据协议；cppType 仅用于进程内的类型转换检查。
	 */
	struct AssetCodec {
		AssetLoader loader;
		uint32_t payloadVersion = 1;
		std::function<bool(const AssetLoadResult&, std::vector<std::byte>&, std::string&)> encode;
	};
}
