#include "catpch.h"

#include "AssetPipeline/Importers/ShaderImporter.h"
namespace Catalpa::Tools {
	bool ShaderImporter::Import(const ShaderCompileRequest& request, ShaderCompileResult& output, std::string& error) {
		if (request.cancellation.IsCancellationRequested()) {
			error = "shader import cancelled";
			return false;
		}
		return ShaderCompileJob::Run(request, output, error);
	}
}
