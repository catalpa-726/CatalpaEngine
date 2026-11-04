#pragma once
#include "ShaderCompiler/ShaderCompileJob.h"
namespace Catalpa::Tools {
    // S04 shader专用导入；通用多类型导入调度仍归后续AssetPipeline阶段。
    class ShaderImporter final {
    public: static bool Import(const ShaderCompileRequest&,ShaderCompileResult&,std::string& error);
    };
}
