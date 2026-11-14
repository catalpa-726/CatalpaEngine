#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "AssetEditors/MaterialEditor.h"
#include "Graphs/GraphCanvas.h"
#include "MaterialGraph/MaterialGraphCompiler.h"
#include "Core/Jobs/JobHandle.h"

namespace Catalpa::Editor {
	class MaterialGraphEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override {
			return doc.TypeKey() == "material_graph" && bool(doc.Source());
		}
		/**
		 * @brief 从当前文档读取材质图数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
		/**
		 * @brief 结束材质图文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于等待编译作业结束并释放图预览资源的宿主。
		 */
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
