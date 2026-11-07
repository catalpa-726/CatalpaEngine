#pragma once
#include "Assets/AssetContainerReader.h"
namespace Catalpa {
    // 格式 2 为格式 1 的 zstd 封装；明确记录解压长度和原容器 SHA-256。
    CATALPA_API bool WriteCompressedAssetContainer(const AssetContainer&, std::vector<std::byte>&,
                                                   std::string& error, size_t maxBytes = 64 * 1024 * 1024);
    CATALPA_API bool ReadCompressedAssetContainer(std::span<const std::byte>, AssetContainer&,
                                                  std::string& error, size_t maxPayloadBytes);
} // namespace Catalpa
