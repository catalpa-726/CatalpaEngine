#include "catpch.h"

#include "AssetPipeline/Importers/AnimationImporter.h"
#include "AssetPipeline/Importers/ParticleImporter.h"
#include "AssetPipeline/Importers/TerrainImporter.h"
#include "AssetPipeline/Importers/PhysicsNavigationImporter.h"
#include "AssetPipeline/Importers/AudioImporter.h"
#include "AssetPipeline/Importers/UIDocumentImporter.h"
#include "AssetPipeline/AssetImportRegistry.h"

#include "AssetPipeline/Importers/GltfImporter.h"
#include "AssetPipeline/Importers/AssimpImporter.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "AssetPipeline/Importers/AuthorImporter.h"
#include "AssetPipeline/Importers/FontImporter.h"

namespace Catalpa::Tools {
	namespace {
		void LowercaseExtension(std::string& extension) {
			std::transform(extension.begin(), extension.end(), extension.begin(),
			    [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
		}
	}

	std::string AssetSourceExtension(const std::filesystem::path& source) {
		auto name = AssetPathText(source.filename());
		LowercaseExtension(name);
		for (const auto suffix : {".scene.json", ".prefab.json", ".matgraph.json", ".behavior.json", ".script.json",
		         ".material.json", ".skeleton.json", ".animclip.json", ".animation.json", ".skin.json",
		         ".blendtree.json", ".particle.json", ".collision.json", ".physics-material.json", ".navmesh.json",
		         ".ui.json", ".terrain.json"})
			if (name.ends_with(suffix))
				return std::string(suffix).substr(0, std::string(suffix).size() - 5);
		auto extension = AssetPathText(source.extension());
		LowercaseExtension(extension);
		return extension;
	}

	bool AssetImportRegistry::Register(
	    std::string ext, std::shared_ptr<const IAssetImporter> importer, std::string& e) {
		LowercaseExtension(ext);
		if (ext.size() < 2 || ext[0] != '.' || !importer || m_Importers.contains(ext)) {
			e = "Invalid or duplicate importer extension";
			return false;
		}
		m_Importers.emplace(std::move(ext), std::move(importer));
		e.clear();
		return true;
	}

	const IAssetImporter* AssetImportRegistry::Find(const std::filesystem::path& p) const {
		const auto it = m_Importers.find(AssetSourceExtension(p));
		return it == m_Importers.end() ? nullptr : it->second.get();
	}

	AssetImportRegistry AssetImportRegistry::Defaults() {
		AssetImportRegistry r;
		std::string e;
		for (const auto& [extension, type] : std::initializer_list<std::pair<const char*, const char*>>{
		         {".skeleton", "skeleton"}, {".animclip", "animation_clip"}, {".animation", "animation_clip"},
		         {".skin", "mesh_skin_binding"}, {".blendtree", "blend_tree"}})
			if (!r.Register(extension, std::make_shared<AnimationImporter>(type), e))
				throw std::logic_error(e);
		for (const auto& [extension, type] : std::initializer_list<std::pair<const char*, const char*>>{
		         {".collision", "collision_shape"}, {".physics-material", "physics_material"}, {".navmesh", "navmesh"}})
			if (!r.Register(extension, std::make_shared<PhysicsNavigationImporter>(type), e))
				throw std::logic_error(e);
		if (!r.Register(".terrain", std::make_shared<TerrainImporter>(), e) ||
		    !r.Register(".particle", std::make_shared<ParticleImporter>(), e) ||
		    !r.Register(".ui", std::make_shared<UIDocumentImporter>(), e))
			throw std::logic_error(e);
		auto audio = std::make_shared<AudioImporter>();
		for (auto extension : {".wav", ".mp3", ".flac"})
			if (!r.Register(extension, audio, e))
				throw std::logic_error(e);
		auto gltf = std::make_shared<GltfImporter>();
		auto assimp = std::make_shared<AssimpImporter>();
		auto texture = std::make_shared<TextureImporter>();
		auto font = std::make_shared<FontImporter>();
		for (auto ext : {".ttf", ".otf", ".ttc"})
			if (!r.Register(ext, font, e))
				throw std::logic_error(e);
		for (const auto& [extension, type] : std::initializer_list<std::pair<const char*, const char*>>{
		         {".scene", "scene"}, {".prefab", "prefab"}, {".lua", "script"}, {".script", "script"},
		         {".behavior", "behavior_graph"}, {".matgraph", "material_graph"}, {".material", "material"}})
			if (!r.Register(extension, std::make_shared<AuthorImporter>(type), e))
				throw std::logic_error(e);
		for (auto ext : {".gltf", ".glb"})
			if (!r.Register(ext, gltf, e))
				throw std::logic_error(e);
		for (auto ext : {".fbx", ".obj"})
			if (!r.Register(ext, assimp, e))
				throw std::logic_error(e);
		for (auto ext : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"})
			if (!r.Register(ext, texture, e))
				throw std::logic_error(e);
		return r;
	}
}
