#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 将跨格式材质参数写入统一材质定义。预览使用单独的派生 ID 和着色器参数。
    class MaterialImporter final {
      public:
        static AssetId PreviewId(const AssetBuildContext& c, std::string_view key) {
            return c.Identity(std::string(key) + "/preview");
        }

        static bool Definition(const ImportedMaterial&, AssetId shader, bool preview, std::string& json,
                               std::string& error);
        // glTF枚举是跨导入器可复核的源协议值；输出领域采样策略，不持有RHI对象。
        static bool SamplerFlags(uint32_t wrapU, uint32_t wrapV, uint32_t minFilter, uint32_t magFilter,
            uint32_t& output, std::string& error);
    };
}
