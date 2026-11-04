#pragma once
#include "catpch.h"
#include "Core/Jobs/CancellationToken.h"
#include "Shaders/ShaderAsset.h"
namespace Catalpa::Tools {
    struct ShaderCompileRequest {
        std::filesystem::path sourceRoot, sourceFile, sdkRoot;
        std::map<std::string,std::string,std::less<>> defines;
        CancellationToken cancellation;
    };
    struct ShaderCompileResult {
        std::shared_ptr<const ShaderAsset> asset;
        std::vector<std::string> dependencies;
        std::string diagnostics;
    };
    // 有界离线作业：源快照/锁定工具→编译→Invariant派生→validator→唯一反射。
    class ShaderCompileJob final {
    public:
        static bool Run(const ShaderCompileRequest&,ShaderCompileResult& output,std::string& error);
        static bool Fingerprint(const ShaderCompileRequest&,std::string& output,std::string& error);
    };
    namespace ShaderToolDetail {
        std::string PathText(const std::filesystem::path&);
        bool ReadFile(const std::filesystem::path&,size_t,std::vector<std::byte>&,std::string&);
        bool RunProcess(const std::filesystem::path& exe,const std::vector<std::wstring>& arguments,
            const std::filesystem::path& diagnosticsFile,CancellationToken,std::string& diagnostics,std::string& error);
        // 插入的装饰只是派生候选，完整合法性必须随后由 spirv-val 判定。
        bool AddPositionInvariant(std::span<const uint32_t>,std::vector<uint32_t>&,std::string&);
    }
}
