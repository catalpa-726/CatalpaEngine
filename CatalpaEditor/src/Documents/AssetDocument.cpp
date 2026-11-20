#include "Documents/AssetDocument.h"

#include "Documents/DocumentRecovery.h"
#include "Assets/BuiltinAssetIds.h"
#include "Core/Serialization/AuthorJson.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa::Editor {
	AssetDocument::AssetDocument(StableId id, AssetId asset, std::string type, std::string source)
	    : Document(id), m_Asset(asset), m_Version(1), m_Type(std::move(type)),
	      m_Source(std::make_shared<const std::string>(std::move(source))) {}

	bool AssetDocument::ReplaceSource(const std::shared_ptr<const std::string>& expected,
	    std::shared_ptr<const std::string> next, std::string& error) {
		if (!next || !m_Source || m_Source != expected || next->size() > DocumentRecovery::AuthorLimit(TypeKey())) {
			error = "Source draft changed or exceeds the author limit";
			return false;
		}
		m_Source = std::move(next);
		return true;
	}

	bool AssetDocument::Serialize(std::string& output, std::string& error) const try {
		if (m_Source) {
			output = *m_Source;
			return true;
		}
		if (!m_Material || !ValidateMaterialInstance(*m_Material, error)) {
			if (error.empty())
				error = "Image sources are read-only";
			return false;
		}
		const auto& material = *m_Material;
		std::string text;
		if (!WriteMaterialInstance(material, text, error))
			return false;
		/* 从物体检查器打开已烘焙的标准材质时，保存为新项目也能直接打开的作者引用。 */
		auto definition = material.definition.GetId();
		if (definition == BuiltinAssetIds::CookedPbrDefinition ||
		    definition == BuiltinAssetIds::CookedUnlitDefinition) {
			auto author = AuthorJson::Json::parse(text);
			author["definitionId"] =
			    (definition == BuiltinAssetIds::CookedPbrDefinition ? BuiltinAssetIds::DefaultPbrDefinition
			                                                        : BuiltinAssetIds::DefaultUnlitDefinition)
			        .ToString();
			author["definitionVersion"] = 1;
			output = author.dump(2);
		} else
			output = text;
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	AssetDocument::AssetDocument(StableId id, AssetVersionRef<MaterialInstance> source)
	    : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()), m_Material(source.GetShared()) {}

	AssetDocument::AssetDocument(StableId id, AssetVersionRef<ImageAsset> source)
	    : Document(id), m_Asset(source.GetId()), m_Version(source.GetVersion()), m_Image(std::move(source)) {}

	bool AssetDocument::ReplaceRecipe(const std::shared_ptr<const Tools::AssetBuildRequest>& expected,
	    std::shared_ptr<const Tools::AssetBuildRequest> next, std::string& error) {
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
