#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Shaders/ShaderAsset.h"
#include "Assets/AssetRecord.h"
namespace Catalpa {
    class AssetManager;
    // 只注册同一AssetManager的shader解码器，不建立平行资源所有者。
    class CATALPA_API ShaderLibrary final {
    public:
        static bool RegisterAssetLoader(AssetManager&, std::string& error);
        static bool EncodePayload(const ShaderAsset&, std::vector<std::byte>& output, std::string& error);
        static bool DecodePayload(std::span<const std::byte>, std::shared_ptr<const ShaderAsset>& output, std::string& error);
        // 仅名称决定身份；内容与工具链决定版本。路径使用源根相对的规范 UTF-8 '/' 表示。
        static bool BuiltinId(std::string_view relativeSource, AssetId& output, std::string& error);
        // 读取部署容器头建立同一资产注册表，不在 Runtime 编译或反射 Shader。
        static bool ReadBuiltinCatalog(const std::filesystem::path& absoluteRoot, std::string_view mount,
            std::vector<AssetRecord>& output, std::string& error);
    };
}
