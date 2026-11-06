#pragma once
#include "MaterialGraph/MaterialGraphSource.h"
#include "ShaderCompiler/ShaderCompileJob.h"

namespace Catalpa::Tools {
    struct MaterialGraphCompileRequest {
        MaterialGraphSource graph;
        std::filesystem::path templateRoot, sdkRoot, derivedRoot;
        AssetId shader;
        CancellationToken cancellation;
    };
    struct MaterialGraphCompileResult {
        std::shared_ptr<const ShaderAsset> shader;
        std::string structure, diagnostics;
        std::map<std::string, StableId, std::less<>> nodeFiles;
    };
    class MaterialGraphCompiler {
      public:
        static bool Compile(const MaterialGraphCompileRequest&, MaterialGraphCompileResult&, std::string& error);
        static bool Generate(const MaterialGraphSource&, std::string_view surfaceTemplate,
                             std::string& slang, std::map<std::string, StableId, std::less<>>& sourceMap,
                             std::string& error);
        static bool Definition(const MaterialGraphSource&, AssetId shader, std::string& json, std::string& error);
        static std::vector<AssetId> Textures(const MaterialGraphSource&);
        // 工具部署复制源模块；Runtime 仍只读取 cooked ShaderAsset。
        static bool DeployTemplates(const std::filesystem::path& sourceRoot,
                                    const std::filesystem::path& destination, std::string& error);
    };
}
