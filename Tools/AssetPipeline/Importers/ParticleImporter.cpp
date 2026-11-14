#include "catpch.h"

#include "AssetPipeline/Importers/ParticleImporter.h"

#include "Particles/ParticleEffect.h"
namespace Catalpa::Tools {
	bool ParticleImporter::Import(AssetBuildContext& context, AssetImportResult& output, std::string& error) const try {
		std::vector<std::byte> bytes;
		if (!context.Read(context.request.source, bytes, error))
			return false;
		ParticleEffect value;
		std::string text;
		if (!ReadParticleEffect({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, value, error) ||
		    !WriteParticleEffect(value, text, error))
			return false;
		const auto id = context.Identity("particle_effect");
		const auto payload = std::as_bytes(std::span(text.data(), text.size()));
		AssetImportResult result;
		result.primary = id;
		result.assets.push_back({id, "particle_effect", "Particle effect",
		    ImportedAuthorProduct{"particle_effect", {payload.begin(), payload.end()}, {value.material}}});
		result.mappings.push_back({id, "particle_effect", "Particle effect", "particle_effect"});
		output = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Tools */
