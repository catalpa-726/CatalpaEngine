#pragma once

#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/** @brief 用本机 Roslyn 将一个 C# 源资产编译为 .NET Framework 行为程序集。 */
	bool CompileCSharp(AssetBuildContext& context, std::span<const std::byte> source, std::vector<std::byte>& assembly,
	    std::string& error);
}
