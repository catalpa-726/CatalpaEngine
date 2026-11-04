#pragma once
#include "ShaderCompileJob.h"
namespace Catalpa::Tools {
    class SlangCompiler final {
    public: static bool Compile(const ShaderCompileRequest&,const std::filesystem::path& temporaryRoot,
        std::vector<uint32_t>& words,std::vector<std::string>& dependencies,std::string& diagnostics,std::string& error);
        // Slang辅助反射只核对入口/stage；binding/offset事实始终来自最终SPIR-V。
        static bool CheckEntryReflection(std::string_view json,const ShaderMetadata&,std::string& error);
    };
}
