#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "AssetEditors/MaterialEditor.h"
#include "Graphs/GraphCanvas.h"
#include "MaterialGraph/MaterialGraphCompiler.h"
#include "Core/Jobs/JobHandle.h"

namespace Catalpa::Editor {
    class MaterialGraphEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override { return doc.TypeKey() == "material_graph" && bool(doc.Source()); }
        void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
        void Close(Application&) override;
      private:
        struct Task {
            std::mutex mutex;
            Tools::MaterialGraphCompileResult result;
            std::string error;
            uint64_t generation = 0;
        };
        bool Commit(AssetDocument&, const Tools::MaterialGraphSource&, std::string&);
        void EditGraph(AssetDocument&, Tools::MaterialGraphSource&, const PanelText&, std::string&);
        void EditToolbar(AssetDocument&, Tools::MaterialGraphSource&, const PanelText&, std::string&);
        void EditCanvas(AssetDocument&, Tools::MaterialGraphSource&, std::string&);
        void EditProperties(AssetDocument&, Tools::MaterialGraphSource&, const PanelText&, std::string&);
        void UpdateCompile(Application&, AssetDocument&, const Tools::MaterialGraphSource&, std::string&);
        bool PublishPreview(Application&, AssetDocument&, const Tools::MaterialGraphSource&, std::string&);
        GraphCanvas m_Canvas;
        MaterialEditor m_Preview;
        std::unique_ptr<AssetDocument> m_PreviewDocument;
        StableId m_Document, m_Selected;
        std::shared_ptr<Task> m_Task;
        JobHandle m_Job;
        uint64_t m_Generation = 0, m_Revision = 0;
        std::string m_Requested, m_GoodStructure, m_Failed, m_PreviewSource, m_Diagnostic;
        std::shared_ptr<const ShaderAsset> m_GoodShader;
        AssetVersionRef<ShaderAsset> m_PublishedShader;
        AssetId m_ShaderId, m_DefinitionId, m_MaterialId;
        std::array<char, 64> m_Texture{};
        int m_Width = 4, m_Semantic = 0;
    };
}
