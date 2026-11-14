#include "catpch.h"

#include "ShaderCompiler/SpirvValidator.h"
namespace Catalpa::Tools {
	bool SpirvValidator::Validate(const std::filesystem::path& sdk, const std::filesystem::path& spirv,
	    const std::filesystem::path& log, CancellationToken token, std::string& diagnostics, std::string& error) {
		return ShaderToolDetail::RunProcess(sdk / L"Bin/spirv-val.exe",
		    {L"--target-env", L"vulkan1.3", spirv.wstring()}, log, token, diagnostics, error);
	}
}
