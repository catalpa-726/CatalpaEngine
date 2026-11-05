#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 索引指向不可变构建版本。命中前校验输入、构建清单、来源报告和容器依赖。
    class AssetBuildCache final {
      public:
        // 验证失败视为未命中，保留调用者的上下文和原结果。
        static bool Load(AssetBuildContext&, std::string_view signature, AssetBuildResult&,
                         std::string& error);
        // 主清单发布后更新索引，索引写入失败只影响下次缓存命中。
        static bool Save(const AssetBuildContext&, std::string_view signature, const AssetBuildResult&,
                         std::string& error);
    };
}
