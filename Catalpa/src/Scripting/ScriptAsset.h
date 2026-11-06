#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"

namespace Catalpa {
    struct ScriptAsset {
        uint32_t schemaVersion = 1;
        std::string source;
        std::map<std::string, AssetId, std::less<>> modules;
        // 运行产物绑定容器中已核实的模块版本；作者 JSON 只保存上面的逻辑身份。
        std::map<std::string, AssetVersionRef<ScriptAsset>, std::less<>> moduleVersions;
    };
    CATALPA_API bool ReadScriptAsset(std::string_view, ScriptAsset&, std::string& error);
    CATALPA_API bool WriteScriptAsset(const ScriptAsset&, std::string&, std::string& error);
    CATALPA_API bool RegisterScriptAssetLoader(class AssetManager&, std::string& error);
}
