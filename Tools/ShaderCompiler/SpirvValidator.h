#pragma once
#include "ShaderCompileJob.h"
namespace Catalpa::Tools {
	class SpirvValidator final {
	public:
		static bool Validate(const std::filesystem::path& sdkRoot, const std::filesystem::path& finalSpirv,
		    const std::filesystem::path& diagnosticsFile, CancellationToken, std::string& diagnostics,
		    std::string& error);
	};
}
