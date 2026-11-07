#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"
#include "UI/FontAsset.h"
#include "Images/ImageAsset.h"

namespace Catalpa {
    // UI 包只保存受检的逻辑相对路径；运行时不回读作者目录或操作系统文件。
    struct UIDocumentAsset {
        std::string entry;
        std::map<std::string, std::string, std::less<>> files;
        std::map<std::string, AssetId, std::less<>> imageIds, fontIds;
        std::map<std::string, std::string, std::less<>> values;
        std::map<std::string, AssetVersionRef<ImageAsset>, std::less<>> images;
        std::map<std::string, AssetVersionRef<FontAsset>, std::less<>> fonts;
    };
    CATALPA_API bool IsUIRelativePath(std::string_view) noexcept;
    CATALPA_API bool IsUIUtf8(std::string_view) noexcept;
    CATALPA_API bool ValidateUIDocument(const UIDocumentAsset&, std::string& error);

    class AssetManager;
    class CATALPA_API UIDocumentLoader final {
      public:
        static bool Encode(const UIDocumentAsset&, std::vector<std::byte>&, std::string& error);
        static bool Register(AssetManager&, std::string& error);
    };
} // namespace Catalpa
