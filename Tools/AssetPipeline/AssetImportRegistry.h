#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    // 扩展名不区分大小写，每种格式固定使用注册的解析器并保留其错误。
    class AssetImportRegistry final {
      public:
        bool Register(std::string extension, std::shared_ptr<const IAssetImporter>, std::string& error);
        const IAssetImporter* Find(const std::filesystem::path&) const;
        static AssetImportRegistry Defaults();

      private:
        std::map<std::string, std::shared_ptr<const IAssetImporter>, std::less<>> m_Importers;
    };
}
