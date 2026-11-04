#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
    // 配置 JSON 的有界适配；读写失败均保留调用者原输出。
    class CATALPA_API JsonArchive final {
    public:
        // 解析和结果分配全部在候选内完成，成功只交换独占结果；失败保留旧指针/值树。
        static bool Parse(std::string_view text, std::unique_ptr<ArchiveValue>& output,
            ArchiveError& error, const ArchiveLimits& limits = {});
        static bool Write(const ArchiveValue& value, std::string& output,
            ArchiveError& error, const ArchiveLimits& limits = {});
    };
}
