#include "Documents/AssetDocument.h"

namespace Catalpa::Editor {
    AssetDocument::AssetDocument(StableId id, AssetVersionRef<MaterialInstance> source)
        : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()),
          m_Material(source.GetShared()) {}

    AssetDocument::AssetDocument(StableId id, AssetVersionRef<ImageAsset> source)
        : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()), m_Image(std::move(source)) {}

    bool AssetDocument::ReplaceRecipe(const std::shared_ptr<const Tools::AssetBuildRequest>& expected,
                                      std::shared_ptr<const Tools::AssetBuildRequest> next,
                                      std::string& error) {
        if (!next || !m_Recipe || m_Recipe != expected) {
            error = "Import settings changed outside this transaction";
            return false;
        }
        m_Recipe = std::move(next);
        error.clear();
        return true;
    }

    bool AssetDocument::ReplaceMaterial(const std::shared_ptr<const MaterialInstance>& expected,
                                        std::shared_ptr<const MaterialInstance> next, std::string& error) {
        if (!next || !m_Material || m_Material != expected) {
            error = "Material draft has changed";
            return false;
        }
        m_Material = std::move(next);
        error.clear();
        return true;
    }
}
