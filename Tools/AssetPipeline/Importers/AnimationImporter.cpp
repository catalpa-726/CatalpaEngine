#include "catpch.h"

#include "AssetPipeline/Importers/AnimationImporter.h"

#include "Animation/AnimationAssetLoader.h"

namespace Catalpa::Tools {
	bool AnimationImporter::Import(AssetBuildContext& context, AssetImportResult& output, std::string& error) const
	    try {
		std::vector<std::byte> bytes;
		if (!context.Read(context.request.source, bytes, error) ||
		    !context.WorkspaceFits(bytes.size() *
		            ((bytes.size() >= 5 &&
		                 (std::memcmp(bytes.data(), "CSKN", 4) == 0 || std::memcmp(bytes.data(), "CANI1", 5) == 0))
		                    ? 4
		                    : 12),
		        error))
			return false;
		const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		std::string canonical;
		std::vector<AssetId> dependencies;
		if (m_Type == "skeleton") {
			SkeletonAsset value;
			if (!ReadSkeleton(source, value, error) || !WriteSkeleton(value, canonical, error))
				return false;
		} else if (m_Type == "animation_clip") {
			AnimationClip value;
			if (!ReadAnimationClip(source, value, error) || !EncodeAnimationClip(value, canonical, error))
				return false;
			dependencies.push_back(value.skeleton);
		} else if (m_Type == "mesh_skin_binding") {
			MeshSkinBinding value;
			if (!ReadMeshSkinBinding(source, value, error) || !EncodeMeshSkinBinding(value, canonical, error))
				return false;
			dependencies = {value.skeleton, value.mesh};
		} else if (m_Type == "blend_tree") {
			BlendTreeAsset value;
			if (!ReadBlendTree(source, value, error) || !WriteBlendTree(value, canonical, error))
				return false;
			dependencies.push_back(value.skeleton);
			for (const auto& node : value.nodes)
				if (node.kind == BlendNodeKind::Clip)
					dependencies.push_back(node.clip);
		} else {
			error = "Unsupported animation author domain";
			return false;
		}
		std::sort(dependencies.begin(), dependencies.end());
		dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
		const auto id = context.Identity(m_Type);
		const auto payload = std::as_bytes(std::span(canonical.data(), canonical.size()));
		if (!context.ReserveWorking(payload.size(), error))
			return false;
		AssetImportResult result;
		result.primary = id;
		result.assets.push_back({id, m_Type, m_Type,
		    ImportedAuthorProduct{m_Type, {payload.begin(), payload.end()}, std::move(dependencies)}});
		result.mappings.push_back({id, m_Type, m_Type, m_Type});
		output = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Tools */
