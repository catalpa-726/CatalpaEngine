#include "catpch.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/GltfImporter.h"
#include "AssetPipeline/Importers/AssimpImporter.h"
#include "AssetPipeline/Importers/TextureImporter.h"

namespace Catalpa::Tools {
    namespace {
        void LowercaseExtension(std::string& extension) {
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
        }
    }

    std::string AssetSourceExtension(const std::filesystem::path& source) {
        auto extension = AssetPathText(source.extension());
        LowercaseExtension(extension);
        return extension;
    }

    bool AssetImportRegistry::Register(std::string ext, std::shared_ptr<const IAssetImporter> importer,
                                       std::string& e) {
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
        auto gltf = std::make_shared<GltfImporter>();
        auto assimp = std::make_shared<AssimpImporter>();
        auto texture = std::make_shared<TextureImporter>();
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
