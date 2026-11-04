#pragma once

#include "catpch.h"
#include "Assets/AssetId.h"
#include "Assets/AssetLocator.h"
#include "Assets/AssetType.h"

namespace Catalpa {

    // 注册表中的持久化元数据；不持有领域实例、加载请求或设备对象。
    struct AssetRecord {
        AssetId id;
        AssetType type;
        AssetLocator locator;
        // 非零的内容版本，由作者/构建流程提供；不是 runtime generation 或加载请求序号。
        uint64_t version = 0;
        std::vector<AssetId> dependencies;
        // 可选 SHA-256：空表示未声明，非空必须是 64 位小写十六进制；注册不代替文件校验。
        std::string contentHash;
    };

}
