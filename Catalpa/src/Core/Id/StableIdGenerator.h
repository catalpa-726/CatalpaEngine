#pragma once

#include "Core/Base.h"
#include "Core/Id/StableId.h"

namespace Catalpa {
    // 新作者身份使用系统随机UUIDv4；加载/重新cook保留原身份，失败不发布部分值。
    [[nodiscard]] CATALPA_API bool GenerateStableId(StableId& output, std::string& error);
}
